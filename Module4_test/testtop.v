module testtop (
    input        CLOCK_50,
    input  [3:0] KEY,        // KEY[0] is Reset
    input  [9:0] SW,         // SW[2:0] Selects Data to Display
    output [6:0] HEX0,
    output [6:0] HEX1,
    output [6:0] HEX2,
    output [6:0] HEX3,
    output [6:0] HEX4,       // Used for Axis Indicator (A, b, C)
    output [6:0] HEX5,       // Used for Mode Indicator (A, G, t)
    
    // I2C Pins
    output       FPGA_I2C_SCL,
    inout        FPGA_I2C_SDAT 
);

    wire reset_n;
    assign reset_n = KEY[0];

    // Wires to hold all sensor data
    wire [15:0] ax, ay, az; 
    wire [15:0] gx, gy, gz; 
    wire [15:0] temp;       
    
    // Registers to hold display settings
    reg [15:0] display_data;
    reg [6:0]  mode_char; // For HEX5
    reg [6:0]  axis_char; // For HEX4

    // 1. Instantiate your Controller
    mpu6050_controller my_accel (
        .clk(CLOCK_50),
        .reset_n(reset_n),
        .accel_x(ax), .accel_y(ay), .accel_z(az),
        .temp(temp),
        .gyro_x(gx), .gyro_y(gy), .gyro_z(gz),
        .i2c_scl(FPGA_I2C_SCL),
        .i2c_sda(FPGA_I2C_SDAT)
    );

    // 2. Multiplexer to select display data using SW[2:0]
    always @(*) begin
        case (SW[2:0])
            // --- ACCELEROMETER (Shows 'A' on HEX5) ---
            // Let's keep X, Y, Z (H, y, 2) for Accel, or we can match Gyro style
            // I'll leave Accel as is, or you can change to A, b, C too.
            // Current: Accel displays 'A' then Axis
            3'b000: begin 
                display_data = ax; 
                mode_char = 7'b000_1000; // 'A'
                axis_char = 7'b000_1001; // 'X' (H)
            end 
            3'b001: begin 
                display_data = ay; 
                mode_char = 7'b000_1000; // 'A'
                axis_char = 7'b001_0001; // 'y'
            end 
            3'b010: begin 
                display_data = az; 
                mode_char = 7'b000_1000; // 'A'
                axis_char = 7'b010_0100; // 'Z' (2)
            end 
            
            // --- GYROSCOPE (Shows 'G' on HEX5) ---
            // UPDATED: Now shows A, b, C on HEX4
            3'b011: begin 
                display_data = gx; 
                mode_char = 7'b100_0010; // 'G' (Capital G)
                axis_char = 7'b000_1000; // 'A'
            end 
            3'b100: begin 
                display_data = gy; 
                mode_char = 7'b100_0010; // 'G'
                axis_char = 7'b000_0011; // 'b'
            end 
            3'b101: begin 
                display_data = gz; 
                mode_char = 7'b100_0010; // 'G'
                axis_char = 7'b100_0110; // 'C'
            end 
            
            // --- TEMPERATURE (Shows 't' on HEX5) ---
            3'b110: begin 
                display_data = temp;  
                mode_char = 7'b000_0111; // 't'
                axis_char = 7'b111_1111; // Blank
            end 
            
            // Default
            default: begin 
                display_data = 16'hFFFF; 
                mode_char = 7'b111_1111; 
                axis_char = 7'b111_1111; 
            end 
        endcase
    end

    // 3. Connect 7-Segment Decoders for Data
    seven_seg_decoder u0 (.bcd_in(display_data[3:0]),   .hex_out(HEX0));
    seven_seg_decoder u1 (.bcd_in(display_data[7:4]),   .hex_out(HEX1));
    seven_seg_decoder u2 (.bcd_in(display_data[11:8]),  .hex_out(HEX2));
    seven_seg_decoder u3 (.bcd_in(display_data[15:12]), .hex_out(HEX3));

    // 4. Connect Indicators
    assign HEX4 = axis_char; 
    assign HEX5 = mode_char; 

endmodule


// --- Helper Module: 7-Segment Decoder ---
module seven_seg_decoder (
    input [3:0] bcd_in,
    output [6:0] hex_out
);
    // Active-low: 0 = ON, 1 = OFF
    // Mapping: gfe_dcba
    reg [6:0] hex;
    
    always @(*) begin
        case (bcd_in)
            4'h0: hex = 7'b100_0000; // 0
            4'h1: hex = 7'b111_1001; // 1
            4'h2: hex = 7'b010_0100; // 2
            4'h3: hex = 7'b011_0000; // 3
            4'h4: hex = 7'b001_1001; // 4
            4'h5: hex = 7'b001_0010; // 5
            4'h6: hex = 7'b000_0010; // 6
            4'h7: hex = 7'b111_1000; // 7
            4'h8: hex = 7'b000_0000; // 8
            4'h9: hex = 7'b001_0000; // 9
            4'hA: hex = 7'b000_1000; // A
            4'hB: hex = 7'b000_0011; // b
            4'hC: hex = 7'b100_0110; // C
            4'hD: hex = 7'b010_0001; // d
            4'hE: hex = 7'b000_0110; // E
            4'hF: hex = 7'b000_1110; // F
            default: hex = 7'b111_1111; // Off
        endcase
    end
    
    assign hex_out = hex;
endmodule