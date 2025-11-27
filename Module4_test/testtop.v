module testtop (
    input         CLOCK_50,
    input  [3:0]  KEY,       // KEY[0] is Reset
    input  [9:0]  SW,        // SW[1:0] Selects Axis
    output [6:0]  HEX0,
    output [6:0]  HEX1,
    output [6:0]  HEX2,
    output [6:0]  HEX3,
    output [6:0]  HEX4,
    output [6:0]  HEX5,
    
    // I2C Pins
    output        FPGA_I2C_SCL,
    inout         FPGA_I2C_SDAT 
);

    wire reset_n;
    assign reset_n = KEY[0];

    // Wires to hold accelerometer data
    wire [15:0] acc_x, acc_y, acc_z;
    
    // Register to hold the data currently selected for display
    reg [15:0] display_data;

    // 1. Instantiate your Accelerometer Controller
    mpu6050_controller my_accel (
        .clk(CLOCK_50),
        .reset_n(reset_n),
        .accel_x(acc_x),
        .accel_y(acc_y),
        .accel_z(acc_z),
        .i2c_scl(FPGA_I2C_SCL),
        .i2c_sda(FPGA_I2C_SDAT)
    );

    // 2. Multiplexer to select which data to show based on Switches
    always @(*) begin
        case (SW[1:0])
            2'b00: display_data = acc_x; // SW is 00 -> Show X
            2'b01: display_data = acc_y; // SW is 01 -> Show Y
            2'b10: display_data = acc_z; // SW is 10 -> Show Z
            default: display_data = 16'hFFFF; // Error pattern
        endcase
    end

    // 3. Connect 7-Segment Decoders (Displaying 16-bit Hex)
    // HEX0 = Lowest 4 bits
    seven_seg_decoder u0 (.bcd_in(display_data[3:0]),   .hex_out(HEX0));
    // HEX1 = Bits 4-7
    seven_seg_decoder u1 (.bcd_in(display_data[7:4]),   .hex_out(HEX1));
    // HEX2 = Bits 8-11
    seven_seg_decoder u2 (.bcd_in(display_data[11:8]),  .hex_out(HEX2));
    // HEX3 = Upper 4 bits
    seven_seg_decoder u3 (.bcd_in(display_data[15:12]), .hex_out(HEX3));

    // Blank out HEX4 and HEX5 (Active Low, so all 1s = Off)
    assign HEX4 = 7'b1111111; 
    assign HEX5 = 7'b1111111; 

endmodule


// --- Helper Module: 7-Segment Decoder (Hex Support) ---
module seven_seg_decoder (
    input [3:0] bcd_in,
    output [6:0] hex_out
);
    // Active-low: 0 = ON, 1 = OFF
    // Mapping: gfe_dcba
    reg [6:0] hex;
    
    always @(*) begin
        case (bcd_in)
            //                  gfe_dcba
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
            4'hB: hex = 7'b000_0011; // b (lowercase)
            4'hC: hex = 7'b100_0110; // C
            4'hD: hex = 7'b010_0001; // d (lowercase)
            4'hE: hex = 7'b000_0110; // E
            4'hF: hex = 7'b000_1110; // F
            default: hex = 7'b111_1111; // Off
        endcase
    end
    
    assign hex_out = hex;
endmodule