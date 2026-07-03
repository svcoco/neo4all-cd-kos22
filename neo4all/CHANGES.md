# Neo4ALL CD RC4 — Port KOS 2.2.x / GCC 13

## 1. Actualización a KOS 2.2.2

La base de código original compilaba contra KOS ~1.2.x. Los cambios para compilar limpio contra KOS 2.2.2 / GCC 13:

**Makefile.dc**
- Reescrito para el sistema de build de KOS 2.x (`$(KOS_BASE)/Makefile.rules`, `kos-cc`, `kos-c++`).
- SDL portado desde KOS-ports (ya no se linkea contra la versión bundled de KOS 1.x).
- `SDL_image` reemplazado por shim mínimo (KOS-ports no incluye SDL_image).

**Código C/C++**
- Eliminados todos los usos de `register` (324 ocurrencias) — ilegal en C++17/GCC 13.
- `irq_exception_t` renombrado a `irq_handler_t` según la API de KOS 2.x.
- `MENU_MUSIC` desactivado (dependía de funciones de audio eliminadas en KOS 2.x).
- `glTexEnvi` eliminado: GLdc 1.1 no implementa `GL_TEXTURE_ENV` — se usa el contexto PVR directamente.

**KGL → GLdc**
- Todos los symbols de KGL (`kglInit`, `kglVertex3f`, etc.) migrados a GLdc 1.1 (`glKosInitEx`, `glVertex3f`, etc.).
- El contexto PVR de tiles (`gl_poly_cxt`) se define explícitamente en `sprgl.cpp` — GLdc no expone ningún global interno equivalente al que usaba KGL.

**Init PVR**
- `pvr_prealloc_neo4all_textures()` extraído como función separada. Se llama antes de `glKosInitEx` para pre-reservar VRAM de tiles/fonts antes de que GLdc consuma el heap completo.
- Doble `pvr_init()` (Neo4ALL + GLdc): KOS detecta y omite la segunda llamada — comportamiento inofensivo con GLdc 1.1.

---

## 2. Renderización 240p

**Detección de cable y resolución nativa**
- `vid_check_cable()` en el arranque: VGA → 640×480, composite/SCART → 320×240 NTSC.
- `vid_set_mode(DM_320x240, PM_RGB565)` se llama antes de `pvr_init` para que PVR configure correctamente la rejilla de tiles y el stride del framebuffer (640 bytes/línea, no 1280).
- `PVR_SET(PVR_SCALER_CFG, 0x400)` fuerza scaler 1:1 exacto (sin interpolación bilineal entre líneas) para imagen nítida en CRT.

**Render PVR nativo en `video_flip()`**
- En DC, `video_flip()` no usa `glTexImage2D` + swap de GLdc. En su lugar:
  - `pvr_txr_load_ex(screen, neo4all_screen_pvr_buffer, 320, 240, PVR_TXRLOAD_16BPP)` sube el framebuffer a VRAM.
  - Quad explícito via `pvr_dr_*` (direct render) cubre el viewport completo sin clipping.
- Esto elimina la dependencia de GLdc para el blit del framebuffer principal y evita la corrupción de colores de `GL_UNSIGNED_SHORT_5_6_5` en GLdc 1.1 (se usa ARGB1555 nativo del PVR).

**SDL surface**
- Máscaras ARGB1555: `R=0x7C00, G=0x03E0, B=0x001F, A=0x8000` en DC (vs RGB565 en no-DC).

---

## 3. Optimización de rendimiento

**Arranque directo (AUTORUN)**
- `Makefile.dc`: `AUTORUN=YES` y `MENU=YES` (este último necesario para evitar errores de linkage con `init_text`).
- `menu_main.cpp`: bloque `#ifdef AUTORUN` al inicio de `run_mainMenu()` — fija `frameskip=0`, región USA, y llama directamente a `try_to_list_files()` sin mostrar menú.
- Resultado: el juego arranca directamente desde el disco sin interacción del usuario.

**FPS en VMU — estadísticas acumuladas**
- `main.cpp`: función `vmu_draw_fps()` que usa `vmufb_*` API de KOS 2.2.x. Render 1×/segundo (cero impacto en rendimiento de gameplay). Layout: cuatro líneas de texto 4×6 con estadísticas acumuladas desde arranque: CUR (FPS actual), MIN (mínimo histórico), MAX (máximo histórico), AVG (promedio de todos los samples). Reset automático único a los 60s desde el primer sample — descarta las pantallas de carga, character select y VS screen (que corren a 60fps estables) antes de acumular datos de gameplay real. Se presenta en todos los VMUs conectados via `maple_enum_type` + `vmufb_present`.
- **Baseline verificado en hardware** (primera pelea completa, post-reset): MIN 39fps / MAX 60fps / AVG 45fps. El MAX de 60fps confirma que el emulador alcanza el techo en momentos tranquilos del fight; el MIN de 39fps ocurre en combate intenso y no era capturable por estimación visual.

**STDOUTPUT desactivado**
- `config.mk`: `#STDOUTPUT=1` comentado — elimina todos los `console_printf`/`console_puts` del binario final.
- Requiere `make clean` para forzar recompilación completa (las flags de compilador no cambian timestamps de .o).

**Tile/font cache — hash tables más grandes con XOR-fold**
- `tile_cache.h`: `TCACHE_HASH_SIZE` 701 → 1024 → 2048; `tcache_hash` = `((key)^(key>>16)) & (TCACHE_HASH_SIZE-1)`.
- `font_cache.h`: `FCACHE_HASH_SIZE` 127 → 256; `fcache_hash` = `((key)^(key>>16)) & 255`.
- El factor dominante en el costo de lookup no es el `%` vs `&` (36 vs 1 ciclo) sino los **D-cache misses por chain walk**: cada nodo en la chain accede a una posición aleatoria en `cache_tile[7680]` (120KB), garantizando un miss en la D-cache de 8KB del SH4 (~50 ciclos/miss). Con 701 buckets el chain avg era 11 nodos; con 1024 baja a 7.5; con 2048 baja a 3.75 — 50% menos misses respecto a 1024.
- El XOR-fold `(key ^ (key>>16))` mezcla los 16 bits altos (tileno/col) con los bajos (color/fontno), evitando que todas las variantes de color del mismo tile colapsen en el mismo bucket.
- Resultado acumulado verificado en hardware (AVG como métrica principal — MIN no comparable por cambio de timer de medición): AVG 47fps con TCACHE 2048 vs AVG 45fps baseline. Mejora neta de +2fps en promedio con percepción subjetiva de mejora confirmada.

**Fix bug `my_z80_cycles`**
- `main.cpp:~956`: el inner loop del Z80 usaba `neo4all_z80_cycles` directamente, ignorando `my_z80_cycles` (que implementa el overclock de los primeros 90 frames para acelerar la carga inicial). Corregido a `zc = my_z80_cycles / NEOGEO_NB_INTERLACE`.

**Fix bug `fcache` usaba `TCACHE_BREAKTIME`**
- `font_cache.h:144`: `fcache_hash_old_cleaner(TCACHE_BREAKTIME)` → `fcache_hash_old_cleaner(FCACHE_BREAKTIME)`.

**`memcard_update` throttling**
- `main.cpp:~1012`: `memcard_update()` se llamaba 60 veces/segundo. Cambiado a `if (!(neogeo_frameskip_count & 0x3F))` — aproximadamente 1 vez/segundo.

**`char fullmode` → `int` en draw.cpp**
- `video_draw_screen1()`: `char fullmode` → `int fullmode`. Evita sign-extension en comparaciones en SH4.

---

### Intentos fallidos (verificados en hardware real)

**`USE_SQ=1` — Corrupción total de sprites**
- Efecto en hardware: sprites completamente corruptos; 5fps en Flycast.
- Causa: `draw_tile.s` ya implementa el copy a VRAM via store queues internamente. El código assembly toma el puntero `br` (dirección VRAM), calcula `QACR0 = (addr >> 26) & 0x1C` y la dirección SQ `= 0xe0000000 | (addr & 0x03ffffe0)`, y escribe via P4. Con `USE_SQ=1` activado, `create_tile()` le pasa `neo4all_texture_buffer` (sysRAM, `0x8Cxxxxxx`) en lugar de la dirección VRAM, produciendo QACR0 incorrecto y escritura al offset VRAM equivocado. Además, `pvr_txr_load()` posterior copia el buffer sysRAM (vacío, no fue escrito) sobre la VRAM correcta. El flag `USE_SQ` fue diseñado para la versión C de `draw_tile` (non-DC); en DC no debe activarse nunca.

**`CACHE_INLINE=1` — Thrashing de I-cache, regresión grave de FPS**
- Efecto en hardware: 24–34fps (caída desde 46–57fps de base).
- Causa: `videogl.cpp` define internamente `CACHE_STATIC_INLINE` como vacío cuando `CACHE_INLINE` no viene del entorno, convirtiendo las funciones de caché en funciones normales exportadas. Al activar `CACHE_INLINE=1` en `config.mk`, `CACHE_STATIC_INLINE` pasa a ser `static __inline__`, forzando la expansión inline de `tcache_hash_insert` y `tcache_hash_old_cleaner` (loop de 512 buckets × chain walk) directamente dentro del hot path `video_draw_spr`. El SH4 del Dreamcast tiene I-cache de 8KB direct-mapped (256 líneas de 32 bytes); el code-bloat resultante la satura, causando thrashing constante en el loop de render. El código del cleaner, aunque se ejecuta raramente, ocupa líneas de I-cache que desplazan al código del camino caliente.

**`TCACHE_BREAKTIME / FCACHE_BREAKTIME` 16→32 — Doble trabajo del cleaner**
- Efecto: contribuye a la regresión de FPS (fue revertido junto con `CACHE_INLINE`).
- Causa: al aumentar el tiempo de retención de tiles en caché de 16 a 32 frames, cuando el pool de slots libres se agota, `tcache_hash_old_cleaner(32)` no encuentra tiles suficientemente viejos para evictar y cae al fallback `tcache_hash_old_cleaner(1)`. Resultado: el cleaner (O(TCACHE_SIZE)) se ejecuta dos veces por evento de pool-exhaustion en lugar de una. Con BREAKTIME=16 original, la primera llamada libera tiles de frames 17+ y frecuentemente es suficiente.

**`FCACHE_HASH_SIZE` 256 → 512 — Regresión grave de AVG**
- Efecto en hardware: AVG 41fps (vs AVG 47fps con TCACHE 2048 activo). Caída de 6fps en AVG, MAX bajó de 60 a 57fps.
- Causa: con TCACHE 2048 (8KB de tabla) + FCACHE 512 (2KB de tabla) el total de tablas de punteros asciende a 10KB — supera la D-cache de 8KB del SH4. Las dos tablas compiten entre sí y con los arrays de nodos por los mismos sets de cache, causando conflictos sistémicos. FCACHE 256 (1KB) es el límite viable junto con TCACHE 2048.

**`pref` prefetch en chain walk de tcache/fcache — Regresión de AVG y MIN**
- Efecto en hardware: MIN 32 / MAX 61 / AVG 42fps (vs baseline MIN 39 / MAX 60 / AVG 45fps). Percepción subjetiva de mayor lentitud confirmada.
- Causa: en la D-cache direct-mapped de 8KB del SH4, precargar el nodo `p->next` mediante `pref @Rn` evicta los datos del nodo actual — con 120KB de `cache_tile` comprimidos en 8KB de cache, los nodos actuales y futuros casi siempre mapean al mismo set y se desplazan mutuamente. El resultado es pagar el costo de la instrucción `pref` (1 ciclo) más un miss adicional en el nodo actual, sin ocultar la latencia del miss futuro. La D-cache direct-mapped hace que el prefetch sea contraproducente en estructuras de datos enlazadas con acceso aleatorio.

**Reordenamiento de struct + alineación de arrays a 32 bytes — Sin mejora, MIN regresión**
- Efecto en hardware: MIN 31 / MAX 61 / AVG 45fps (vs baseline MIN 39 / MAX 60 / AVG 45fps). AVG idéntico, MIN empeoró 8fps.
- Causa probable: `__attribute__((aligned(32)))` sobre `cache_tile` y `cache_font` desplaza las posiciones de ambos arrays en el segmento BSS, alterando los offsets relativos entre los arrays de nodos (120KB + 64KB) y las tablas hash (4KB + 1KB). En la D-cache direct-mapped de 8KB del SH4, esos offsets determinan qué estructuras compiten por los mismos sets — la nueva alineación introdujo más conflictos de cache set entre las tablas que los que eliminó al evitar accesos cross-cache-line. Lección: en caches direct-mapped pequeñas, los efectos secundarios del layout de memoria superan al beneficio teórico del alineamiento de campos.

**`-O3` en `sprgl.cpp` — Thrashing de I-cache, regresión grave de FPS**
- Efecto en hardware: MIN 30 / MAX 58 / AVG 40fps (vs baseline MIN 39 / MAX 60 / AVG 45fps). Caída de 5fps en AVG y 9fps en MIN.
- Causa: `-O3` activa loop unrolling e inlining agresivo, expandiendo el código compilado de `sprgl.cpp`. Como este módulo está en el hot path de render (se ejecuta cada frame por cada sprite en pantalla), el código expandido no cabe en los 8KB de I-cache direct-mapped del SH4, causando thrashing constante. El mismo mecanismo que destruyó el rendimiento con `CACHE_INLINE=1`. En módulos de render del SH4, `-O2` es superior a `-O3` porque mantiene el código compacto y cache-friendly.

**Hash PoT con tablas pequeñas (512/128 buckets) — Regresión por D-cache**
- Efecto en hardware: 35–51fps con `& 511` (perf3), 39–50fps con XOR-fold `& 511` (perf4). Ambas configuraciones por debajo del baseline 46–57fps.
- Causa: reducir el número de buckets de 701→512 (TCACHE) y 127→128 (FCACHE) alarga los chains. El factor dominante no es el costo del `%` (36 ciclos SH4) sino los D-cache misses por chain walk: cada nodo accede a una posición aleatoria en `cache_tile[7680]` (120KB), garantizando ~50 ciclos de miss en la D-cache de 8KB del SH4. Con 512 buckets el chain avg sube de 11 a 15 nodos: 4 nodos × 50 ciclos × 1000 lookups/frame = 200,000 ciclos adicionales/frame. El ahorro de `%`→`&` (35 ciclos × 1000 = 35,000 ciclos) no compensa. La solución correcta fue aumentar los buckets (1024/256), no sólo cambiar el operador.

---

## 4. Trabajo pendiente

- **Menú de juego**: reimplementar como texto puro PVR (`bfont` / `vmufb_print_string`), sin SDL surface ni PNG. La implementación actual tiene corrupción de textura porque GLdc rechaza `GL_RGBA + GL_UNSIGNED_SHORT_1_5_5_5_REV`.
- **Pantalla de Loading**: mismo problema de textura que el menú.
- **`USE_SQ` para DC**: requeriría reescribir `draw_tile.s` para que acepte un buffer sysRAM y haga el SQ copy internamente, o cambiar `create_tile()` para manejar el caso DC por separado.
- **`FM_INLINE=1`**: pendiente de evaluar impacto en tamaño/velocidad.
- **`-O3`**: probar en módulos críticos (`sprgl.cpp`, `draw.cpp`, `draw_fixgl.cpp`).
- **Auto-frameskip por estadísticas PVR**: usar `pvr_get_stats()` para ajuste dinámico.
- **PVR DMA para tiles**: `USE_DMA=1` — requiere gestión de doble buffer para evitar tear.

---

## 5. Generador de CDI

El generador construye una imagen CDI booteable para Dreamcast a partir del ELF compilado y un juego Neo Geo CD en formato BIN/CUE.

### Estructura de directorios

```
neo4all-cdi-generator/
├── build_cdi.py       # script principal
├── neo4all/           # ELF del emulador (neo4all.elf)
├── neobios/           # BIOS Neo Geo (uni-bios o similar)
├── game/              # juego en formato BIN/CUE
└── output/            # CDI generado
```

### Uso

1. Compilar el emulador:
   ```bash
   cd neo4all-src-rc4/neo4all
   make -f Makefile.dc clean && make -f Makefile.dc
   ```

2. Copiar el ELF al generador:
   ```bash
   cp neo4all.elf ../../../neo4all-cdi-generator/neo4all/neo4all.elf
   ```

3. Generar la imagen:
   ```bash
   cd neo4all-cdi-generator
   python3 build_cdi.py
   ```

4. La imagen queda en `output/`. Copiar a la carpeta compartida con Windows:
   ```bash
   cp output/*.cdi /home/jorge-dev/win-share/
   ```

### Requisitos

- Python 3
- `mkisofs` / `genisoimage`
- `cdi4dc` o herramienta equivalente para generar CDI desde ISO
- BIOS Neo Geo en `neobios/`
- Juego Neo Geo CD en `game/` (BIN + CUE)
