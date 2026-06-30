/*
 * kos_compat_stubs.c
 *
 * Stubs puntuales para simbolos que el GLdc 1.1.0 de kos-ports referencia
 * pero que no existen en este arbol de KOS 2.2.x. Ver guia del proyecto,
 * seccion "ld: cannot find -lGL" / pvr_txr_set_stride, para el diagnostico
 * completo de por que falta y por que un stub vacio es seguro aqui.
 *
 * pvr_txr_set_stride():
 *   Unico call site en GLdc 1.1.0 (GL/platforms/sh4.c, SceneListSubmit):
 *
 *     if (header->meta.texture_is_strided && ...)
 *         pvr_txr_set_stride(header->meta.texture_stride);
 *
 *   Neo4ALL no usa texturas "strided" (todas sus texturas son de tamano
 *   fijo: 512x512, 16x16, 8x8, cargadas via glTexImage2D estandar), por lo
 *   que texture_is_strided nunca se activa y esta funcion nunca se ejecuta
 *   en la practica. KOS 2.2.x ya no expone un setter de stride en tiempo
 *   de ejecucion (el bit PVR_TXRFMT_STRIDE se hornea en el formato de
 *   textura al cargarla), asi que no hay una funcion real de KOS a la cual
 *   reenviar la llamada. El stub solo satisface el enlazador.
 */

#ifdef __cplusplus
extern "C" {
#endif

void pvr_txr_set_stride(int stride)
{
	(void)stride;
}

#ifdef __cplusplus
}
#endif
