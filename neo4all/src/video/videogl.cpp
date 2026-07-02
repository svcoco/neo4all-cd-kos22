#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "video.h"
#include "../neo4all.h"

#ifndef CACHE_INLINE
#define CACHE_INLINE
#define CACHE_STATIC_INLINE
#endif

#include "videogl.h"


static SDL_Surface *gl_screen;

tcache_node_t *tcache_hash_table[TCACHE_HASH_SIZE];
tcache_node_t cache_tile[TCACHE_SIZE];
tcache_node_t *first_tile, *last_tile;

fcache_node_t *fcache_hash_table[FCACHE_HASH_SIZE];
fcache_node_t cache_font[FCACHE_SIZE];
fcache_node_t *first_font, *last_font;

unsigned neo4all_filter=NEO4ALL_FILTER_NONE;
#ifdef DREAMCAST
unsigned neo4all_pvr_filter=NEO4ALL_PVR_FILTER_NONE;
#endif

TILE_LIST tile_list[TCACHE_SIZE+FCACHE_SIZE];
unsigned n_tile_list=0;
unsigned n_font_list=0;

#ifndef DREAMCAST
GLint tile_opengl_tex[TCACHE_SIZE+FCACHE_SIZE];
GLint black_opengl_tex;
#endif

GLint screen_texture;

unsigned ntiles=0;
unsigned tiles_fail=0;

void *neo4all_texture_buffer=NULL;
void *neo4all_texture_buffer_free=NULL;
void *neo4all_texture_real_buffer=NULL;
void *neo4all_font_real_buffer=NULL;
void *neo4all_texture_surface=NULL;
void *neo4all_black_texture_buffer=NULL;
float tile_z=TILE_Z_INIT;
unsigned neo4all_glframes=8;

#ifdef DREAMCAST
int   neo4all_hw_width  = 640;
int   neo4all_hw_height = 480;
float neo4all_scale_x   = 2.0f;
float neo4all_move_x    = 16.0f;
#endif

static void init_cache(void) {
    glEnable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    if (neo4all_texture_buffer==NULL)
    {
        /* On DC, pvr_prealloc_neo4all_textures() already populated all
           neo4all_texture_* pointers before init_cache() is reached.
           This branch is only taken on non-DC builds. */
        neo4all_texture_buffer_free=calloc(64+(16*16*2),1);
        neo4all_texture_buffer=(void *)(((((unsigned)neo4all_texture_buffer_free)+32)/32)*32);
#ifndef DREAMCAST
        neo4all_texture_real_buffer=calloc(16*16*2,TCACHE_SIZE);
        neo4all_font_real_buffer=calloc(8*8*2,FCACHE_SIZE);
        neo4all_texture_surface=calloc(512*512,2);
        neo4all_black_texture_buffer=calloc(16*16,2);
#endif
    }

#ifndef DREAMCAST
    {
        int i;
        for(i=0;i<TCACHE_SIZE+FCACHE_SIZE;i++)
            glGenTextures(1,(GLuint *)&tile_opengl_tex[i]);
        glGenTextures(1,(GLuint *)&black_opengl_tex);
    }
#endif
    video_reset_gl();
}

static void free_cache(void) {
#ifdef DREAMCAST
    /* tile+font+black: single pvr_mem block, font/black pointers are inside it */
    pvr_mem_free(neo4all_texture_real_buffer);
    /* sysRAM allocation from pvr_prealloc_neo4all_textures() */
    free(neo4all_texture_surface);
#else
    free(neo4all_texture_real_buffer);
    free(neo4all_font_real_buffer);
    free(neo4all_texture_surface);
    free(neo4all_black_texture_buffer);
#endif
    free(neo4all_texture_buffer_free);
    neo4all_texture_buffer=NULL;
}

#ifdef DREAMCAST
static void pvr_prealloc_neo4all_textures(void) {
    /* GLdc's _glInitTextures() (texture.c:601) calls
         pvr_mem_malloc(pvr_mem_available() - 64 KB)
       consuming the entire texture heap on glKosInitEx. Pre-allocating
       Neo4ALL's tile/font buffers first leaves GLdc with only the remainder
       (~880 KB), enough for screen_texture (512x512x2 = 512 KB).
       GLdc's InitGPU() calls pvr_init() a second time; KOS detects this,
       emits a warning, and skips the second init — benign, architecturally
       unavoidable with GLdc 1.1's API. */
    pvr_init_params_t params = {
        { PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_32 },
        2560 * 256, /* matches GLdc default: sh4.c PVR_VERTEX_BUF_SIZE */
        0,          /* no DMA */
        GL_FALSE,   /* no FSAA */
        1,          /* autosort disabled */
        2           /* opb overflow count */
    };
    pvr_init(&params);

    /* tiles + fonts + black border texture in one PVR block: all three are
       referenced as txr.base by the direct-PVR render path and must live in
       VRAM (draw_tile.s/draw_font.s write them via store queues; the black
       texture is CPU-filled once in neo4all_black_texture()). */
    void *raw = pvr_mem_malloc((16*16*2 * TCACHE_SIZE) + (8*8*2 * FCACHE_SIZE)
                               + (16*16*2));
    if (raw) {
        neo4all_texture_real_buffer  = raw;
        neo4all_font_real_buffer     = (void *)((unsigned)raw + 16*16*2 * TCACHE_SIZE);
        neo4all_black_texture_buffer = (void *)((unsigned)neo4all_font_real_buffer
                                                + 8*8*2 * FCACHE_SIZE);
    }
    /* Framebuffer surface in sysRAM: CPU-written, uploaded to GLdc's VRAM
       pool via glTexImage2D each frame. */
    neo4all_texture_surface      = calloc(512 * 512, 2);
    neo4all_texture_buffer_free  = calloc(64 + (16*16*2), 1);
    neo4all_texture_buffer = (void *)(((((unsigned)neo4all_texture_buffer_free)+32)/32)*32);
}

static void gldc_init(void) {
    GLdcConfig config;
    glKosInitConfig(&config);
    config.autosort_enabled           = GL_FALSE;
    config.initial_op_capacity        = 512;
    config.initial_tr_capacity        = 512;
    config.initial_pt_capacity        = 64;
    config.initial_immediate_capacity = 0;
    glKosInitEx(&config);
}
#endif

SDL_bool init_video_gl(void) {
#ifdef DREAMCAST
    {
        /* KOS pvr_init() reads the CURRENT vid_mode (set by KOS startup at
           DM_640x480 in hardware.c) to configure PVR tile matrices and
           framebuffer stride.  We must call vid_set_mode() for the desired
           resolution BEFORE pvr_prealloc_neo4all_textures() so that pvr_init()
           picks it up.  Calling vid_set_mode AFTER pvr_init leaves pvr_state.w
           at 640, causing pvr_misc.c to restore PVR_RENDER_MODULO to 1280 bytes
           every frame while the video output reads 640 bytes/scanline — the
           result is interleaved content/black scanlines that look like only the
           top quarter of the display is active. */
        int cable = vid_check_cable();
        if (cable == CT_VGA) {
            neo4all_hw_width  = 640;
            neo4all_hw_height = 480;
            neo4all_scale_x   = 2.0f;
            neo4all_move_x    = 16.0f;
            /* KOS startup already set DM_640x480 — no change needed */
        } else {
            neo4all_hw_width  = 320;
            neo4all_hw_height = 240;
            neo4all_scale_x   = 1.0f;
            neo4all_move_x    = 8.0f;
            /* Set 320x240 NOW so pvr_init uses 10x8 tile grid and 640-byte stride */
            vid_set_mode(DM_320x240, PM_RGB565);
        }
    }
    pvr_prealloc_neo4all_textures();  /* pvr_init reads current vid_mode */
    /* pvr_init() sets PVR_SCALER_CFG=0x401 on non-VGA cables ("vertical
       smoothing"): the video scaler resamples adjacent lines to soften
       interlace flicker.  In progressive 240p that interpolation only blurs
       the picture — force exact 1.0 passthrough (0x400 = no filtering). */
    PVR_SET(PVR_SCALER_CFG, 0x400);
    gldc_init();
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 1);
#ifdef DREAMCAST
    gl_screen = SDL_SetVideoMode(neo4all_hw_width, neo4all_hw_height, 16,
                SDL_DOUBLEBUF | SDL_HWSURFACE | SDL_HWPALETTE | SDL_OPENGL);
#else
    gl_screen = SDL_SetVideoMode(VIDEO_GL_WIDTH, VIDEO_GL_HEIGHT, 16,
                SDL_DOUBLEBUF | SDL_HWSURFACE | SDL_HWPALETTE | SDL_OPENGL);
#endif
    if ( gl_screen == NULL)
	return SDL_FALSE;

    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(1);
    glClearDepth(1.0);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#ifdef DREAMCAST
    glViewport(0, 0, neo4all_hw_width, neo4all_hw_height);
#else
    glViewport(0, 0, VIDEO_GL_WIDTH, VIDEO_GL_HEIGHT);
#endif
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glOrtho(0.0, 320.0, 240.0, 0.0, -50.0, 50.0 );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    init_cache();
//    blitter(); used_blitter=0;

#ifdef MENU_ALPHA
#ifdef DREAMCAST
    screen = SDL_CreateRGBSurfaceFrom(neo4all_texture_surface, 320, 240, 16, 1024, 0x7C00, 0x3E0, 0x1F, 0x8000);
#else
    screen = SDL_CreateRGBSurfaceFrom(neo4all_texture_surface, 320, 240, 16, 1024, 0x1F, 0x3E0, 0x7C00, 0x8000);
#endif
    SDL_FillRect(screen,NULL,0x8000);
#else
    screen = SDL_CreateRGBSurfaceFrom(neo4all_texture_surface, 320, 240, 16, 1024 , 0xF800, 0x7E0, 0x1F, 0);
    SDL_FillRect(screen,NULL,0);
#endif

    glGenTextures(1,(GLuint *)&screen_texture);
    glBindTexture(GL_TEXTURE_2D,screen_texture);
    loadTextureParams();
    return SDL_TRUE;
}

void video_fullscreen_toggle_gl(void)
{
#ifndef DREAMCAST
	SDL_WM_ToggleFullScreen(gl_screen);
#endif

}


void video_reset_gl(void)
{
    unsigned i;
    tcache_hash_init();
    for(i=0;i<TCACHE_SIZE;i++)
	    cache_tile[i].rec=(unsigned short *)(((unsigned)neo4all_texture_real_buffer)+i*16*16*2);
    tcache_hash_init();

    fcache_hash_init();
    for(i=0;i<FCACHE_SIZE;i++)
	    cache_font[i].rec=(unsigned short *)(((unsigned)neo4all_font_real_buffer)+i*8*8*2);
    fcache_hash_init();
    n_tile_list=0;
    n_font_list=0;
    used_blitter=0;
}

void neogeo_adjust_filter(int filter)
{
	if (filter)
		neo4all_filter=NEO4ALL_FILTER_BILINEAR;
	else
		neo4all_filter=NEO4ALL_FILTER_NONE;
#ifdef DREAMCAST
	if (filter)
		neo4all_pvr_filter=NEO4ALL_PVR_FILTER_BILINEAR;
	else
		neo4all_pvr_filter=NEO4ALL_PVR_FILTER_NONE;
#endif
}


void neo4all_black_texture(void)
{
	unsigned *p=(unsigned *)neo4all_black_texture_buffer;
	int i;
	for(i=0;i<(16*16/2);i++)
		*p++=0x80008000;
}
