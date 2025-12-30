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

// Include your hardware headers here
#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "socal/alt_gpio.h"
#include "hps_0.h"

// --- Hardware Constants ---
#define HW_REGS_BASE        (ALT_LWFPGASLVS_OFST) 
#define HW_REGS_SPAN        (0x00200000) 
#define HW_REGS_MASK        (HW_REGS_SPAN - 1)

// --- Global Sensor Data ---
static volatile int16_t g_ax = 0;
static volatile int16_t g_ay = 0;
static volatile int16_t g_az = 0;
static volatile int16_t g_gx = 0;
static volatile int16_t g_gy = 0;
static volatile int16_t g_gz = 0; 
static volatile int16_t g_temp = 0;

// ============================================================================
// HTTP SERVER THREAD
// ============================================================================
void *http_server_thread(void *arg)
{
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char recv_buf[1024]; 
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

    printf("Reactor Run Server listening on port 8080\n");

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        if (client_fd < 0) continue;

        int read_len = read(client_fd, recv_buf, sizeof(recv_buf) - 1);
        if (read_len > 0) {
            recv_buf[read_len] = '\0'; 

            // Snapshot global vars
            int16_t ax = g_ax, ay = g_ay, az = g_az;
            int16_t gx = g_gx, gy = g_gy, gz = g_gz;
            int16_t temp_raw = g_temp;

            // --- TEMPERATURE CALIBRATION ---
            float temp_c = (float)(temp_raw + 5072) / 128.0f; 
            float temp_f = (temp_c * 9.0f / 5.0f) + 32.0f;

            char body[16384]; 
            char header[512];

            if (strstr(recv_buf, "GET /data")) {
                // CSV Data API: AX,AY,AZ,GX,GY,GZ,TEMP_C,TEMP_F,RAW
                snprintf(body, sizeof(body), "%d,%d,%d,%d,%d,%d,%.2f,%.2f,%d", 
                         ax, ay, az, gx, gy, gz, temp_c, temp_f, temp_raw);
                
                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                    (int)strlen(body));
            } 
            else {
                // Serve the Game Page HTML/JS
                snprintf(body, sizeof(body),
                    "<html><head><title>Reactor Run (Sphere)</title>"
                    "<style>"
                    "body{font-family:'Courier New',monospace;text-align:center;background:#111;color:#0f0;overflow:hidden;}"
                    "#gameCanvas{background:#000;border:4px solid #333;margin-top:20px;box-shadow: 0 0 20px rgba(0,255,0,0.2);}"
                    "#hud{display:flex;justify-content:center;flex-wrap:wrap;width:900px;margin:10px auto;font-size:14px;color:#aaa;background:#222;padding:10px;border-radius:5px;}"
                    ".sensor-group{margin:0 20px;text-align:left;}"
                    ".bar-container{width:150px;height:20px;background:#333;border:1px solid #555;display:inline-block;vertical-align:middle;}"
                    "#temp-bar{height:100%%;background:#ff0000;width:50%%;transition: width 0.2s, background-color 0.2s;}"
                    ".val{color:#fff;font-weight:bold;margin-left:5px;font-family:monospace;}"
                    "h3{margin:0 0 5px 0;color:#0f0;font-size:16px;border-bottom:1px solid #444;}"
                    "</style>"
                    "<script>"
                    "var car = {x: 50, y: 50, w: 20, h: 30, angle: 0, speed: 0};"
                    "var temp_c = 0; var temp_raw = 0;"
                    "var ax=0, ay=0, az=0;"
                    "var gx=0, gy=0, gz=0;" 
                    "var maze = [" 
                    "  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],"
                    "  [1,2,0,0,0,0,1,0,0,0,0,0,0,0,1],"
                    "  [1,1,1,1,1,0,1,0,1,1,1,1,1,0,1],"
                    "  [1,0,0,0,0,0,0,0,0,0,0,0,1,0,1],"
                    "  [1,0,1,1,1,1,1,1,1,1,1,0,1,0,1],"
                    "  [1,0,1,0,0,0,0,0,0,0,0,0,1,0,1],"
                    "  [1,0,1,0,1,1,1,1,1,1,1,1,1,0,1],"
                    "  [1,0,0,0,0,0,0,0,0,0,0,0,0,0,1],"
                    "  [1,1,1,1,1,1,1,1,1,1,1,1,1,3,1],"
                    "  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]"
                    "];" 
                    "var TILE_SIZE = 40;"
                    "var gameOver = false;"
                    
                    "function init() {"
                    "  canvas = document.getElementById('gameCanvas');"
                    "  ctx = canvas.getContext('2d');"
                    "  setInterval(fetchData, 50);" 
                    "  requestAnimationFrame(draw);"
                    "}"

                    "function fetchData() {"
                    "  var x = new XMLHttpRequest();"
                    "  x.onreadystatechange = function(){"
                    "    if(this.readyState==4 && this.status==200){"
                    "      var v = this.responseText.split(',');"
                    
                    // --- Parse ALL Sensors again ---
                    "      ax = parseInt(v[0]); ay = parseInt(v[1]); az = parseInt(v[2]);"
                    "      gx = parseInt(v[3]); gy = parseInt(v[4]); gz = parseInt(v[5]);"
                    "      temp_c = parseFloat(v[6]);"
                    "      temp_raw = parseInt(v[8]);"
                    "      "
                    // --- Update Full Dashboard ---
                    "      document.getElementById('val-ax').innerText = ax;"
                    "      document.getElementById('val-ay').innerText = ay;"
                    "      document.getElementById('val-az').innerText = az;"
                    "      document.getElementById('val-gx').innerText = gx;"
                    "      document.getElementById('val-gy').innerText = gy;"
                    "      document.getElementById('val-gz').innerText = gz;"
                    "      document.getElementById('val-temp').innerText = temp_c.toFixed(1) + ' C (Raw: ' + temp_raw + ')';"
                    "    }"
                    "  };"
                    "  x.open('GET','/data',true); x.send();"
                    "}"

                    "function draw() {"
                    "  if(gameOver) { requestAnimationFrame(draw); return; }"
                    "  ctx.clearRect(0, 0, canvas.width, canvas.height);"

                    // --- PHYSICS ---
                    "  var steer = gz;"
                    "  var gas = gx;"
                    "  if (Math.abs(steer) < 100) steer = 0;"
                    "  if (Math.abs(gas) < 100) gas = 0;"
                    "  car.angle += steer / 5000.0;" 
                    "  car.speed += gas / 2000.0;" 
                    "  car.speed *= 0.96;"
                    "  var nextX = car.x + Math.cos(car.angle) * car.speed;"
                    "  var nextY = car.y + Math.sin(car.angle) * car.speed;"

                    // --- COLLISION ---
                    "  var col = Math.floor(nextX / TILE_SIZE);"
                    "  var row = Math.floor(nextY / TILE_SIZE);"
                    "  if (maze[row][col] == 1) {"
                    "     car.speed *= -0.5;" 
                    "  } else if (maze[row][col] == 3) {"
                    "     document.getElementById('status').innerText = 'REACTOR SECURE! YOU WIN!';"
                    "     gameOver = true;"
                    "  } else {"
                    "     car.x = nextX; car.y = nextY;"
                    "  }"

                    // --- RENDER MAZE ---
                    "  for(var r=0; r<10; r++) {"
                    "    for(var c=0; c<15; c++) {"
                    "      if(maze[r][c] == 1) {"
                    "        ctx.fillStyle = '#444';"
                    "        ctx.fillRect(c*TILE_SIZE, r*TILE_SIZE, TILE_SIZE, TILE_SIZE);"
                    "        ctx.strokeStyle = '#222'; ctx.strokeRect(c*TILE_SIZE, r*TILE_SIZE, TILE_SIZE, TILE_SIZE);"
                    "      } else if (maze[r][c] == 3) {"
                    "        ctx.fillStyle = '#00ff00';" 
                    "        ctx.fillRect(c*TILE_SIZE, r*TILE_SIZE, TILE_SIZE, TILE_SIZE);"
                    "      }"
                    "    }"
                    "  }"

                    "  ctx.save();"
                    "  ctx.translate(car.x, car.y);"
                    "  ctx.rotate(car.angle);"
                    
                    // --- DRAW 3D SPHERE ---
                    "  var grad = ctx.createRadialGradient(4, -4, 1, 0, 0, 12);"
                    "  grad.addColorStop(0, '#ffffff');"      
                    "  grad.addColorStop(0.3, '#00ccff');"  
                    "  grad.addColorStop(1, '#004466');"    
                    
                    "  ctx.fillStyle = grad;"
                    "  ctx.beginPath();"
                    "  ctx.arc(0, 0, 12, 0, Math.PI * 2);" 
                    "  ctx.fill();"

                    "  ctx.strokeStyle = 'rgba(255,255,255,0.5)';"
                    "  ctx.lineWidth = 2;"
                    "  ctx.beginPath();"
                    "  ctx.moveTo(0, 0);"
                    "  ctx.lineTo(11, 0);" 
                    "  ctx.stroke();"
                    
                    "  ctx.restore();"

                    // --- Temp bar scaling (0C to 60C) ---
                    "  var heat = (temp_c / 60.0) * 100.0;"
                    "  if (heat > 100) heat = 100; if(heat < 0) heat = 0;"
                    
                    "  var r = Math.floor((heat/100)*255);"
                    "  var b = 255 - r;"
                    
                    "  var barElem = document.getElementById('temp-bar');"
                    "  barElem.style.width = heat + '%';"
                    "  barElem.style.backgroundColor = 'rgb('+r+',0,'+b+')';"

                    "  requestAnimationFrame(draw);"
                    "}"
                    "</script>"
                    "</head><body onload='init()'>"
                    "<h1>IoT Gateway</h1>"
                    "<div id='status'>Drive the Sphere to the Green Zone!</div>"
                    
                    "<div id='hud'>"
                    // --- RESTORED ACCEL SECTION ---
                    "  <div class='sensor-group'><h3>ACCEL</h3>"
                    "    <div>X:<span id='val-ax' class='val'>0</span></div>"
                    "    <div>Y:<span id='val-ay' class='val'>0</span></div>"
                    "    <div>Z:<span id='val-az' class='val'>0</span></div>"
                    "  </div>"
                    // --- RESTORED GYRO SECTION ---
                    "  <div class='sensor-group'><h3>GYRO</h3>"
                    "    <div>X:<span id='val-gx' class='val'>0</span></div>"
                    "    <div>Y:<span id='val-gy' class='val'>0</span></div>"
                    "    <div>Z:<span id='val-gz' class='val'>0</span></div>"
                    "  </div>"
                    "  <div class='sensor-group'><h3>ENV</h3>"
                    "    <div>TEMP:<span id='val-temp' class='val'>0</span></div>"
                    "    <div>HEAT (0-60C): <div class='bar-container'><div id='temp-bar'></div></div></div>"
                    "  </div>"
                    "</div>"
                    
                    "<canvas id='gameCanvas' width='600' height='400'></canvas>"
                    "</body></html>"
                );

                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                    (int)strlen(body));
            }

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

    void *accel_x_ptr, *accel_y_ptr, *accel_z_ptr;
    void *gyro_x_ptr, *gyro_y_ptr, *gyro_z_ptr;
    void *temp_ptr;

    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) return 1;
    virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, HW_REGS_BASE);
    if (virtual_base == MAP_FAILED) return 1;

    // MAP SENSORS
    accel_x_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_X_BASE) & (unsigned long)(HW_REGS_MASK));
    accel_y_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Y_BASE) & (unsigned long)(HW_REGS_MASK));
    accel_z_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_ACCEL_Z_BASE) & (unsigned long)(HW_REGS_MASK));

    gyro_x_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_GYRO_A_BASE) & (unsigned long)(HW_REGS_MASK));
    gyro_y_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_GYRO_B_BASE) & (unsigned long)(HW_REGS_MASK));
    gyro_z_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_GYRO_C_BASE) & (unsigned long)(HW_REGS_MASK));

    temp_ptr = virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + PIO_TEMP_BASE) & (unsigned long)(HW_REGS_MASK));

    pthread_t http_thread;
    if (pthread_create(&http_thread, NULL, http_server_thread, NULL) == 0)
        pthread_detach(http_thread);

    printf("Reactor Run (Sphere Version - Full HUD) Started.\n");
    printf("REMEMBER TO HARD REFRESH YOUR BROWSER (Ctrl+F5)\n");

    while (1) {
        // Read All Sensors
        g_ax = (int16_t)(*(volatile uint32_t *)accel_x_ptr & 0xFFFF);
        g_ay = (int16_t)(*(volatile uint32_t *)accel_y_ptr & 0xFFFF);
        g_az = (int16_t)(*(volatile uint32_t *)accel_z_ptr & 0xFFFF);

        g_gx = (int16_t)(*(volatile uint32_t *)gyro_x_ptr & 0xFFFF);
        g_gy = (int16_t)(*(volatile uint32_t *)gyro_y_ptr & 0xFFFF);
        g_gz = (int16_t)(*(volatile uint32_t *)gyro_z_ptr & 0xFFFF);

        g_temp = (int16_t)(*(volatile uint32_t *)temp_ptr & 0xFFFF);

        usleep(50 * 1000);
    }

    munmap(virtual_base, HW_REGS_SPAN);
    close(fd);
    return 0;
}