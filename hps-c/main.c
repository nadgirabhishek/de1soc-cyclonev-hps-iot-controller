#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "socal/alt_gpio.h"
#include "hps_0.h"

#define HW_REGS_BASE (ALT_STM_OFST)
#define HW_REGS_SPAN (0x04000000)
#define HW_REGS_MASK (HW_REGS_SPAN - 1)

static volatile int16_t g_ax = 0;
static volatile int16_t g_ay = 0;
static volatile int16_t g_az = 0;
static volatile int     g_duty = 0;

void *http_server_thread(void *arg)
{
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char recv_buf[1024]; // Buffer to read the browser request
    (void)arg;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) return NULL;
    if (listen(server_fd, 5) < 0) return NULL;

    printf("HTTP server listening on port 8080\n");

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) continue;

        // 1. READ THE REQUEST to see what the browser wants
        int read_len = read(client_fd, recv_buf, sizeof(recv_buf) - 1);
        if (read_len > 0) {
            recv_buf[read_len] = '\0'; // Null terminate

            int16_t ax = g_ax;
            int16_t ay = g_ay;
            int16_t az = g_az;
            int duty = g_duty;

            char body[2048]; // Increased buffer size for JS code
            char header[512];

            // 2. CHECK REQUEST TYPE
            // If browser asks for "/data", send JUST the numbers
            if (strstr(recv_buf, "GET /data")) {
                // Send values as plain text: "ax,ay,az,duty"
                snprintf(body, sizeof(body), "%d,%d,%d,%d", ax, ay, az, duty);
                
                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n" // Plain text data
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n\r\n",
                    (int)strlen(body)
                );
            } 
            // Otherwise, send the MAIN PAGE (Static HTML + JS)
            else {
                snprintf(body, sizeof(body),
                    "<html><head>"
                    "<title>Accel Status</title>"
                    "<style>body{font-family:sans-serif; text-align:center; padding-top:50px;}</style>"
                    "<script>"
                    // JavaScript to fetch data every 2000ms (2 seconds)
                    "setInterval(function(){"
                    "  var x = new XMLHttpRequest();"
                    "  x.onreadystatechange = function(){"
                    "    if(this.readyState==4 && this.status==200){"
                    "      var v = this.responseText.split(',');"
                    "      document.getElementById('ax').innerHTML = v[0];"
                    "      document.getElementById('ay').innerHTML = v[1];"
                    "      document.getElementById('az').innerHTML = v[2];"
                    "      document.getElementById('dt').innerHTML = v[3];"
                    "    }"
                    "  };"
                    "  x.open('GET','/data',true); x.send();"
                    "}, 2000);"
                    "</script>"
                    "</head><body>"
                    "<h1>Accel PWM Status</h1>"
                    // Spans with IDs so JS can find and update them
                    "<p>AX = <span id='ax'>%d</span></p>"
                    "<p>AY = <span id='ay'>%d</span></p>"
                    "<p>AZ = <span id='az'>%d</span></p>"
                    "<p>PWM Duty = <span id='dt'>%d</span>%%</p>"
                    "</body></html>",
                    ax, ay, az, duty
                );

                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: close\r\n\r\n",
                    (int)strlen(body)
                );
            }

            // Send response
            write(client_fd, header, strlen(header));
            write(client_fd, body, strlen(body));
        }
        
        close(client_fd);
    }

    close(server_fd);
    return NULL;
}

int main(void)
{
    int fd;
    void *virtual_base;

    void *h2p_lw_pwm_addr;
    void *h2p_lw_accel_x_addr;
    void *h2p_lw_accel_y_addr;
    void *h2p_lw_accel_z_addr;

    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) return 1;

    virtual_base = mmap(NULL, HW_REGS_SPAN,
                        (PROT_READ | PROT_WRITE),
                        MAP_SHARED,
                        fd, HW_REGS_BASE);

    if (virtual_base == MAP_FAILED) return 1;

    h2p_lw_pwm_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_PWM_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_x_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_X_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_y_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Y_BASE) &
         (unsigned long)(HW_REGS_MASK));

    h2p_lw_accel_z_addr = virtual_base +
        ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Z_BASE) &
         (unsigned long)(HW_REGS_MASK));

    pthread_t http_thread;
    if (pthread_create(&http_thread, NULL, http_server_thread, NULL) == 0)
        pthread_detach(http_thread);

    printf("Starting accel->PWM control loop...\n");

    float prev_mag = 0.0f;
    float smooth_mag = 0.0f;
    int current_duty = 0;

    const float ALPHA = 0.1f;
    const float SCALE = 2000.0f;
    const int MAX_DUTY = 100;
    const int MIN_DUTY = 0;
    const int HYSTERESIS = 2;
    const int MAX_STEP = 2;

    unsigned int loop_count = 0;

    while (1) {
        int16_t ax = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_x_addr & 0xFFFF);
        int16_t ay = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_y_addr & 0xFFFF);
        int16_t az = (int16_t)(*(volatile uint32_t *)h2p_lw_accel_z_addr & 0xFFFF);

        g_ax = ax;
        g_ay = ay;
        g_az = az;

        float fx = (float)ax;
        float fy = (float)ay;
        float fz = (float)az;

        float mag = sqrtf(fx*fx + fy*fy + fz*fz);
        float diff = fabsf(mag - prev_mag);
        prev_mag = mag;

        smooth_mag = ALPHA * diff + (1.0f - ALPHA) * smooth_mag;

        float norm = smooth_mag / SCALE;
        if (norm > 1.0f) norm = 1.0f;
        if (norm < 0.0f) norm = 0.0f;

        int target_duty = (int)(norm * 100.0f);
        if (target_duty > MAX_DUTY) target_duty = MAX_DUTY;
        if (target_duty < MIN_DUTY) target_duty = MIN_DUTY;

        int delta = target_duty - current_duty;
        if (abs(delta) > HYSTERESIS) {
            if (delta > MAX_STEP) delta = MAX_STEP;
            else if (delta < -MAX_STEP) delta = -MAX_STEP;
            current_duty += delta;
        }

        *(volatile uint32_t *)h2p_lw_pwm_addr = (uint32_t)current_duty;
        g_duty = current_duty;

        if ((loop_count++ % 10) == 0)
            printf("AX=%6d AY=%6d AZ=%6d DUTY=%3d%%\n", ax, ay, az, current_duty);

        usleep(100 * 1000);
    }

    munmap(virtual_base, HW_REGS_SPAN);
    close(fd);
    return 0;
}