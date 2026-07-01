#ifndef _VIDEOGL_H_
#define _VIDEOGL_H_

#include "SDL.h"
#include "SDL_opengl.h"

#ifdef DREAMCAST
#include <GL/glkos.h>
#endif

#if defined(CACHE_INLINE) && !defined(CACHE_STATIC_INLINE)
#define CACHE_STATIC_INLINE static __inline__
#endif

#include "tile_cache.h"
#include "font_cache.h"

#ifdef DREAMCAST

/* Display resolution — set at runtime in init_video_gl() via vid_check_cable():
     CT_VGA  (0) → 640×480, 2× integer scale of Neo Geo 320×240
     15 KHz  (1-3) → 320×240, native 240p, 1:1 pixel mapping        */
extern int   neo4all_hw_width;
extern int   neo4all_hw_height;
/* PVR screen-space scale: 2.0/16.0 for VGA, 1.0/8.0 for 240p */
extern float neo4all_scale_x;
extern float neo4all_move_x;

#define TILE_Z_INIT 1.0
#define TILE_Z_INC 0.69

#else
/*
#define VIDEO_GL_WIDTH 800
#define VIDEO_GL_HEIGHT 600

*/
#define VIDEO_GL_WIDTH 640
#define VIDEO_GL_HEIGHT 480
#define TILE_Z_INIT 0.1
#define TILE_Z_INC 0.000069
#endif

typedef struct TILE_LIST{
	short sx, sy, zx, zy;
	char type;
	void *buffer;
}TILE_LIST;

extern TILE_LIST tile_list[TCACHE_SIZE+FCACHE_SIZE];
extern unsigned n_tile_list;
extern unsigned n_font_list;

extern GLint tile_opengl_tex[TCACHE_SIZE+FCACHE_SIZE];
extern GLint black_opengl_tex;


extern float tile_z;

SDL_bool init_video_gl(void);
void video_reset_gl(void);
void neogeo_adjust_filter(int filter);
void video_reset_gl(void);
void video_fullscreen_toggle_gl(void);

#define NEO4ALL_FILTER_NONE	GL_NEAREST
#define NEO4ALL_FILTER_BILINEAR GL_LINEAR

#ifdef DREAMCAST
/* gl_poly_cxt.txr.filter (pvr_poly_cxt_t, API PVR nativa) necesita
   constantes PVR_FILTER_*, no GL_*. neo4all_filter alimenta glTexParameteri()
   (OpenGL real via GLdc) y necesita GL_NEAREST/GL_LINEAR; neo4all_pvr_filter
   es la version paralela para los consumidores PVR (sprgl.cpp, draw_fixgl.cpp). */
#define NEO4ALL_PVR_FILTER_NONE		PVR_FILTER_NONE
#define NEO4ALL_PVR_FILTER_BILINEAR	PVR_FILTER_BILINEAR
#endif

extern unsigned neo4all_filter;
#ifdef DREAMCAST
extern unsigned neo4all_pvr_filter;
#endif

extern unsigned ntiles;
extern unsigned tiles_fail;

extern void *neo4all_texture_buffer;
extern void *neo4all_texture_real_buffer;
extern void *neo4all_texture_l2_buffer;
extern void *neo4all_texture_l2_buffer1;
extern void *neo4all_texture_l2_buffer2;
extern void *neo4all_texture_surface;
extern void *neo4all_black_texture_buffer;
extern unsigned tile_l2_ptr;
extern unsigned neo4all_glframes;

extern void *neo4all_font_real_buffer;
extern void *neo4all_font_l2_buffer;


static __inline__ void loadTextureParams(void)
{
//    glPixelStorei(GL_UNPACK_ROW_LENGTH, 16);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, neo4all_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, neo4all_filter);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

void neo4all_draw_boders(void);
void neo4all_black_texture(void);

#endif
