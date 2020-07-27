#include "os.h"
#include "ux.h"
#include "cx.h"
#include <string.h>

unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

static const bagl_element_t *io_seproxyhal_touch_exit(const bagl_element_t *e);

ux_state_t ux;

static const bagl_element_t bagl_ui_sample_nanos[]={

    {
        {BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000,
         0xFFFFFF, 0, 0},
         NULL
    },
    {
        {BAGL_LABELINE, 0x01, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
         BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
         "Hello, World"
    }
};

static unsigned int bagl_ui_sample_nanos_button(unsigned int button_mask, unsigned int button_mask_counter){

    switch(button_mask){

        case BUTTON_EVT_RELEASED | BUTTON_LEFT | BUTTON_RIGHT:
            io_seproxyhal_touch_exit(NULL);
            break;
    }
    return 0;
}

static const bagl_element_t *io_seproxyhal_touch_exit(const bagl_element_t *e){

    os_sched_exit(0);
    return NULL;
}

unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len) {
    switch (channel & ~(IO_FLAGS)) {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len) {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED) {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        } else {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}

static void ui_idle(void){
    UX_DISPLAY(bagl_ui_sample_nanos, NULL);
}

static void sample_main(void){

    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    for(;;){
        volatile unsigned short sw = 0;
        
        BEGIN_TRY{
            TRY{
                rx = tx;
                tx = 0;

                rx = io_exchange(CHANNEL_APDU | flags, rx);
                flags = 0;

                if(rx == 0){
                    THROW(0x6982);
                }
                if (G_io_apdu_buffer[0] != 0x80) {
                    THROW(0x6E00);
                }

                switch (G_io_apdu_buffer[1]) {
                case 0x00: // reset
                    flags |= IO_RESET_AFTER_REPLIED;
                    THROW(0x9000);
                    break;

                case 0x01: // case 1
                    THROW(0x9000);
                    break;

                case 0x02: // echo
                    tx = rx;
                    THROW(0x9000);
                    break;

                case 0xFF: // return to dashboard
                    goto return_to_dashboard;

                default:
                    THROW(0x6D00);
                    break;
                }
            }
            CATCH_OTHER(e){
                switch (e & 0xF000) {
                case 0x6000:
                case 0x9000:
                    sw = e;
                    break;
                default:
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY{}
        }
        END_TRY;
    }
    return_to_dashboard:
        return;
}

void io_seproxyhal_display(const bagl_element_t *element) {
    io_seproxyhal_display_default((bagl_element_t *)element);
}

unsigned char io_event(unsigned char channel){

    switch (G_io_seproxyhal_spi_buffer[0]) {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT: // for Nano S
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        if (UX_DISPLAYED()) {
            // TODO perform actions after all screen elements have been
            // displayed
        } else {
            UX_DISPLAYED_EVENT();
        }
        break;

    // unknown events are acknowledged
    default:
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent()) {
        io_seproxyhal_general_status();
    }

    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

__attribute__((section(".boot"))) int main(void){

    __asm volatile("cpsie i");

    UX_INIT();

    os_boot();

    BEGIN_TRY{
        TRY{

            io_seproxyhal_init();

            USB_power(0);
            USB_power(1);

            ui_idle();
            sample_main();

        }
        CATCH_OTHER(e){

        }
        FINALLY{

        }
    }
    END_TRY;
}