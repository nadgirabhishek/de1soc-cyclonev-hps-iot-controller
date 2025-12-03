`timescale 1ns/1ps

module tb_accelerometer_controller;

    // --- Testbench signals ---
    reg  clk;
    reg  reset_n;

    // DUT outputs
    wire [15:0] accel_x;
    wire [15:0] accel_y;
    wire [15:0] accel_z;
    wire        i2c_scl;
    wire        i2c_sda;

    // Slave (testbench) open‑drain drive on SDA
    // 0 = pull low, 1 = release (Z)
    reg tb_sda_drive;

    // Combine DUT and TB open‑drain drives on SDA
    // DUT already drives i2c_sda through its own tri‑state.
    // TB can additionally pull it low.
    assign i2c_sda = (tb_sda_drive == 1'b0) ? 1'b0 : 1'bz;

    // Pull‑ups for both lines
    pullup(i2c_sda);
    pullup(i2c_scl);

    // --- Instantiate DUT ---
    accelerometer_controller #(
        .CLK_DIV(20)              // small for sim so SCL is fast
    ) uut (
        .clk     (clk),
        .reset_n (reset_n),
        .accel_x (accel_x),
        .accel_y (accel_y),
        .accel_z (accel_z),
        .i2c_scl (i2c_scl),
        .i2c_sda (i2c_sda)
    );

    // --- 50 MHz clock ---
    initial clk = 1'b0;
    always #10 clk = ~clk;        // 20 ns period

    // ====================================================
    // I2C slave helper tasks
    // ====================================================

    // ACK 1 byte from master (9th bit low)
    task slave_send_ack;
        begin
            // ACK bit starts with SCL low
            @(negedge i2c_scl);
            tb_sda_drive = 1'b0;      // drive SDA low for ACK

            @(posedge i2c_scl);       // SCL high, ACK valid

            @(negedge i2c_scl);       // end ACK bit
            tb_sda_drive = 1'b1;      // release SDA
        end
    endtask

    // Send one data byte to master, MSB first.
    // Master controls SCL. Slave changes SDA on falling edges.
    task slave_send_byte;
        input [7:0] data;
        integer i;
        begin
            for (i = 7; i >= 0; i = i - 1) begin
                @(negedge i2c_scl);
                tb_sda_drive = data[i];   // 0 = pull low, 1 = release
            end

            // After last bit, release so master can ACK/NACK
            @(negedge i2c_scl);
            tb_sda_drive = 1'b1;
        end
    endtask

    // ====================================================
    // Main stimulus
    // ====================================================
    initial begin
        // Init
        reset_n      = 1'b0;      // hold in reset (active‑low)
        tb_sda_drive = 1'b1;      // slave idle (not pulling low)

        // Hold reset a little
        #100;
        reset_n = 1'b1;           // release reset
        #40;

        // Fast‑forward: skip almost all of REFRESH_LIMIT so
        // the FSM leaves IDLE quickly.
        force   uut.refresh_timer = uut.REFRESH_LIMIT - 5;
        @(posedge clk);
        release uut.refresh_timer;

        $display("TB: Reset released, fast‑forwarded timer, waiting for first START...");

        // Wait for START (SDA falling edge while SCL is high)
        @(negedge i2c_sda);
        $display("TB: Detected START at time %t", $time);

        // Device address (write) ACK
        slave_send_ack();
        $display("TB: ACKed device address (WRITE) at %t", $time);

        // Register address (0x32) ACK
        slave_send_ack();
        $display("TB: ACKed register address at %t", $time);

        // Repeated START: SCL high, then SDA falling
        @(posedge i2c_scl);
        @(negedge i2c_sda);
        $display("TB: Detected REPEATED START at %t", $time);

        // Device address (read) ACK
        slave_send_ack();
        $display("TB: ACKed device address (READ) at %t", $time);

        // Send fake data:
        // X = 0x1234, Y = 0x5678, Z = 0x9ABC (low byte first)
        $display("TB: Sending data bytes at %t", $time);

        slave_send_byte(8'h34);   // X_L
        slave_send_byte(8'h12);   // X_H
        slave_send_byte(8'h78);   // Y_L
        slave_send_byte(8'h56);   // Y_H
        slave_send_byte(8'hBC);   // Z_L
        slave_send_byte(8'h9A);   // Z_H

        // Wait for STOP (SDA rising while SCL high).
        // Easiest is just watch for SDA rising.
        @(posedge i2c_sda);
        $display("TB: Detected STOP at %t", $time);

        // Give DUT some time to latch outputs
        #200;

        $display("TB: accel_x = %h, accel_y = %h, accel_z = %h",
                 accel_x, accel_y, accel_z);

        if (accel_x === 16'h1234 &&
            accel_y === 16'h5678 &&
            accel_z === 16'h9ABC) begin
            $display("TB: SUCCESS – data received correctly.");
        end
        else begin
            $display("TB: ERROR – data mismatch!");
        end

        #200;
        $stop;
    end

endmodule
