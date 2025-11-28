#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "socal/alt_gpio.h"
#include "hps_0.h"

#define HW_REGS_BASE ( ALT_STM_OFST )
#define HW_REGS_SPAN ( 0x04000000 )
#define HW_REGS_MASK ( HW_REGS_SPAN - 1 )

int main() {

    void *virtual_base;
    int fd;
    int loop_count;
    int led_direction;
    int led_mask;

    void *h2p_lw_led_addr;

    // NEW: accel PIO addresses
    void *h2p_lw_accel_x_addr;
    void *h2p_lw_accel_y_addr;
    void *h2p_lw_accel_z_addr;

    // open /dev/mem
    if( ( fd = open( "/dev/mem", ( O_RDWR | O_SYNC ) ) ) == -1 ) {
        printf( "ERROR: could not open \"/dev/mem\"...\n" );
        return( 1 );
    }

    virtual_base = mmap( NULL, HW_REGS_SPAN, 
                         ( PROT_READ | PROT_WRITE ), MAP_SHARED, 
                         fd, HW_REGS_BASE );

    if( virtual_base == MAP_FAILED ) {
        printf( "ERROR: mmap() failed...\n" );
        close( fd );
        return( 1 );
    }
    
    // existing LED mapping
    h2p_lw_led_addr = virtual_base + 
        ( ( unsigned long )( ALT_LWFPGASLVS_OFST + PIO_LED_BASE ) & 
          ( unsigned long)( HW_REGS_MASK ) );

    // NEW: map accel PIOs
    h2p_lw_accel_x_addr = virtual_base +
        ( ( unsigned long )( ALT_LWFPGASLVS_OFST + PIO_ACCEL_X_BASE ) & 
          ( unsigned long)( HW_REGS_MASK ) );

    h2p_lw_accel_y_addr = virtual_base +
        ( ( unsigned long )( ALT_LWFPGASLVS_OFST + PIO_ACCEL_Y_BASE ) & 
          ( unsigned long)( HW_REGS_MASK ) );

    h2p_lw_accel_z_addr = virtual_base +
        ( ( unsigned long )( ALT_LWFPGASLVS_OFST + PIO_ACCEL_Z_BASE ) & 
          ( unsigned long)( HW_REGS_MASK ) );

    // toggle LEDs (existing)
    loop_count = 0;
    led_mask = 0x01;
    led_direction = 0;

    while( loop_count < 60 ) {
        
        // LED logic (unchanged)
        *(uint32_t *)h2p_lw_led_addr = ~led_mask;
        usleep( 100*1000 );

        if (led_direction == 0){
            led_mask <<= 1;
            if (led_mask == (0x01 << (PIO_LED_DATA_WIDTH-1)))
                    led_direction = 1;
        } else {
            led_mask >>= 1;
            if (led_mask == 0x01){ 
                led_direction = 0;
                loop_count++;
            }
        }

        // NEW: print accel every 10 counts
        if (loop_count % 10 == 0) {
            int16_t ax = *(uint32_t *)h2p_lw_accel_x_addr & 0xFFFF;
            int16_t ay = *(uint32_t *)h2p_lw_accel_y_addr & 0xFFFF;
            int16_t az = *(uint32_t *)h2p_lw_accel_z_addr & 0xFFFF;

            printf("ACCEL X=%d  Y=%d  Z=%d\n", ax, ay, az);
        }

    } // while

    // cleanup
    if( munmap( virtual_base, HW_REGS_SPAN ) != 0 ) {
        printf( "ERROR: munmap() failed...\n" );
        close( fd );
        return( 1 );
    }

    close( fd );
    return 0;
}
