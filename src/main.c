#include <kernel.h>
#include <gsKit.h>
#include <dmaKit.h>

int main(voi)
{
    GSGLOBAL *gs = gsKit_init_global();

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    u64 black = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00);
    u64 red   = GS_SETREG_RGBAQ(0xFF, 0x00, 0x00, 0x00, 0x00);

    float x = 100.0f;

    while (1) {
        gsKit_clear(gs, black);

        /* ตัวละครชั่วคราว */
        gsKit_prim_sprite(gs, x, 200, x + 32, 264, 0, red);

        x += 2.0f;
        if (x > gs->Width) x = 0;

        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }

    return 0;
}
