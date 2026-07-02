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

**FPS en VMU**
- `main.cpp`: bloque `#ifdef DREAMCAST` en `neogeo_run()` que cuenta frames por segundo y emite `vmu_printf("FPS\n%u", fps)` en todos los VMUs conectados, una vez por segundo.

**STDOUTPUT desactivado**
- `config.mk`: `#STDOUTPUT=1` comentado — elimina todos los `console_printf`/`console_puts` del binario final.
- Requiere `make clean` para forzar recompilación completa (las flags de compilador no cambian timestamps de .o).

**Tile/font cache — hash power-of-two**
- `tile_cache.h`: `TCACHE_HASH_SIZE` 701 → 512; `tcache_hash(key)` de `key % 701` a `(key) & 511`.
- `font_cache.h`: `FCACHE_HASH_SIZE` 127 → 128; `fcache_hash(key)` de `key % 127` a `(key) & 127`.
- En SH4, `%` sobre divisor no-PoT cuesta ~36 ciclos; `&` cuesta 1. La operación se ejecuta cientos de veces por frame.

**Tile/font cache — tiempo de evicción**
- `TCACHE_BREAKTIME` y `FCACHE_BREAKTIME`: 16 → 32 frames. Reduce los spikes de GC del caché.

**CACHE_INLINE**
- `config.mk`: `CACHE_INLINE=1` — las funciones de `tile_cache.h` y `font_cache.h` se generan como `static inline` en cada unidad de traducción que las incluye, en lugar de llamadas a función.

**Fix bug `my_z80_cycles`**
- `main.cpp:~956`: el inner loop del Z80 usaba `neo4all_z80_cycles` directamente, ignorando `my_z80_cycles` (que implementa el overclock de los primeros 90 frames para acelerar la carga). Corregido a `zc = my_z80_cycles / NEOGEO_NB_INTERLACE`.

**Fix bug `fcache` usaba `TCACHE_BREAKTIME`**
- `font_cache.h:144`: `fcache_hash_old_cleaner(TCACHE_BREAKTIME)` → `fcache_hash_old_cleaner(FCACHE_BREAKTIME)`.

**`memcard_update` throttling**
- `main.cpp:~1012`: `memcard_update()` se llamaba 60 veces/segundo. Cambiado a `if (!(neogeo_frameskip_count & 0x3F))` — aproximadamente 1 vez/segundo.

**`char fullmode` → `int` en draw.cpp**
- `video_draw_screen1()`: `char fullmode` → `int fullmode`. Evita sign-extension en comparaciones en SH4.

**Nota sobre `USE_SQ`**
- `USE_SQ=1` causa corrupción de sprites en DC: `draw_tile.s` ya usa store queues internamente (calcula QACR0 y la dirección P4 a partir del puntero VRAM pasado). Activar `USE_SQ` hace que `create_tile()` pase `neo4all_texture_buffer` (sysRAM) en lugar de la dirección VRAM, produciendo un mapeo SQ incorrecto. El flag está diseñado para la versión C de `draw_tile` (non-DC); en DC no debe activarse.

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
