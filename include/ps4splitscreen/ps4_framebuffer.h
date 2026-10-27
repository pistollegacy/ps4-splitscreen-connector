/**
 * @file ps4_framebuffer.h
 * @brief PS4 Framebuffer Management (GNM)
 */

#ifndef PS4SPLITSCREEN_PS4_FRAMEBUFFER_H
#define PS4SPLITSCREEN_PS4_FRAMEBUFFER_H

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/gnm.h>
#include <ps4/videoout.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct framebuffer_manager framebuffer_manager_t;

framebuffer_manager_t* ps4splitscreen_framebuffer_create(uint32_t width, uint32_t height,
                                                          SceGnmColorFormat color_fmt,
                                                          SceGnmDepthStencilFormat depth_fmt,
                                                          uint32_t sample_count,
                                                          uint32_t buffer_count);
void ps4splitscreen_framebuffer_destroy(framebuffer_manager_t* mgr);

SceGnmRenderTarget* ps4splitscreen_framebuffer_get_current_rt(framebuffer_manager_t* mgr);
SceGnmDepthRenderTarget* ps4splitscreen_framebuffer_get_current_depth(framebuffer_manager_t* mgr);
SceGnmTexture* ps4splitscreen_framebuffer_get_current_texture(framebuffer_manager_t* mgr);

void ps4splitscreen_framebuffer_advance(framebuffer_manager_t* mgr);
int ps4splitscreen_framebuffer_present(framebuffer_manager_t* mgr, SceGnmCommandBuffer* cmd);
void ps4splitscreen_framebuffer_wait_vblank(framebuffer_manager_t* mgr);

SceVideoOutHandle ps4splitscreen_framebuffer_get_video_handle(framebuffer_manager_t* mgr);

void ps4splitscreen_framebuffer_setup_draw(framebuffer_manager_t* mgr, 
                                            SceGnmCommandBuffer* cmd,
                                            const float clear_color[4],
                                            float clear_depth,
                                            uint8_t clear_stencil);
void ps4splitscreen_framebuffer_end_draw(framebuffer_manager_t* mgr, SceGnmCommandBuffer* cmd);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_PS4_FRAMEBUFFER_H