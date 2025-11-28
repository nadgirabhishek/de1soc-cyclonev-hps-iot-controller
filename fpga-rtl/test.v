module mpu6050_controller ( // Renamed module for accuracy
    input        clk,
    input        reset_n,

    // Outputs to Processor
    output reg [15:0] accel_x,
    output reg [15:0] accel_y,
    output reg [15:0] accel_z,

    // I2C Physical Lines
    output reg   i2c_scl,
    inout        i2c_sda
);

    parameter CLK_DIV = 500; 

    // --- SENSOR PARAMETERS (UPDATED FOR ICM-20948) ---
    // NOTE: The I2C address might be 0x68 (AD0=Low) or 0x69 (AD0=High).
    // If you still see issues, try changing this to 7'h69.
    localparam SLAVE_ADDR      = 7'h68;    
    
    // In ICM-20948 (Bank 0), Power Mgmt 1 is at 0x06 (was 0x6B in MPU6050)
    localparam REG_PWR_MGMT_1  = 8'h06;    
    
    // Value 0x01 clears sleep and auto-selects the best clock source
    localparam REG_PWR_DATA    = 8'h01;    
    
    // In ICM-20948 (Bank 0), Accel X High Byte is at 0x2D (was 0x3B in MPU6050)
    localparam REG_ADDR_START  = 8'h2D;    

    reg [8:0]  clk_count;
    reg        i2c_tick;
    reg [1:0]  phase;

    reg [4:0]  state;
    reg [4:0]  saved_state;
    reg [2:0]  bit_cnt;
    reg [2:0]  byte_cnt;
    reg [7:0]  data_buffer;
    reg        sda_out;
    reg        sda_en;
    reg        config_done; 

    // Register map for readback
    reg [7:0] rx_data [0:5];

    // Periodic poll timer
    reg [19:0] refresh_timer; 
    localparam REFRESH_LIMIT = 20'd50000;

    // FIX: Strict Open-Drain Logic (Safety)
    assign i2c_sda = (sda_en && sda_out == 1'b0) ? 1'b0 : 1'bz;

    // 4-phase I2C tick generator
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            clk_count <= 0;
            i2c_tick  <= 0;
        end else begin
            if (clk_count == (CLK_DIV/4) - 1) begin
                clk_count <= 0;
                i2c_tick  <= 1;
            end else begin
                clk_count <= clk_count + 1;
                i2c_tick  <= 0;
            end
        end
    end

    // --- FSM states
    localparam IDLE          = 0;
    localparam START         = 1;
    localparam WR_DEV_ADDR   = 2;
    localparam WR_REG_ADDR   = 3;
    localparam WR_REG_DATA   = 4;
    localparam ACK_CHECK     = 5;
    localparam RESTART       = 6;
    localparam RD_DEV_ADDR   = 7;
    localparam READ_DATA     = 8;
    localparam ACK_SEND      = 9;
    localparam STOP          = 10;
    localparam WAIT_CONFIG   = 11;

    reg config_write; 

    // --- Main FSM
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state           <= IDLE;
            saved_state     <= IDLE;
            phase           <= 0;
            i2c_scl         <= 1;
            sda_out         <= 1;
            sda_en          <= 1;
            refresh_timer   <= 0;
            config_done     <= 0;
            config_write    <= 1;
            accel_x         <= 0; accel_y <= 0; accel_z <= 0;
            bit_cnt         <= 0; byte_cnt <= 0;
            data_buffer     <= 8'd0;
        end
        else if (i2c_tick) begin
            phase <= phase + 1'b1;
            case (state)
            IDLE: begin
                i2c_scl <= 1; sda_out <= 1; sda_en <= 1; phase <= 0;
                if (!config_done) begin
                    state <= START;
                    config_write <= 1;
                end else if (refresh_timer < REFRESH_LIMIT) begin
                    refresh_timer <= refresh_timer + 1;
                end else begin
                    refresh_timer <= 0;
                    state    <= START;
                    config_write <= 0;
                end
            end

            START: begin
                case (phase)
                    2'd0: begin sda_out <= 1; i2c_scl <= 1; end 
                    2'd1: begin sda_out <= 0; end // Drop SDA (Start)
                    2'd2: begin i2c_scl <= 0; end // Drop SCL
                    2'd3: begin
                        bit_cnt <= 7;
                        state   <= WR_DEV_ADDR;
                        phase   <= 0;
                    end
                endcase
            end

            WR_DEV_ADDR: begin
                case (phase)
                    2'd0: begin
                        // CRITICAL LOGIC FIX:
                        // First frame is ALWAYS a Write (0), even if we want to read later.
                        // We need to write the register address first.
                        if (bit_cnt == 0) sda_out <= 0; 
                        else sda_out <= SLAVE_ADDR[bit_cnt-1];
                    end
                    2'd1: i2c_scl <= 1;
                    2'd3: begin
                        i2c_scl <= 0;
                        if (bit_cnt == 0) begin
                            sda_en <= 0; 
                            state <= ACK_CHECK; 
                            saved_state <= WR_REG_ADDR; 
                            phase <= 0;
                        end else bit_cnt <= bit_cnt - 1;
                    end
                endcase
            end

            WR_REG_ADDR: begin
                case (phase)
                    2'd0: sda_out <= config_write ? REG_PWR_MGMT_1[bit_cnt] : REG_ADDR_START[bit_cnt];
                    2'd1: i2c_scl <= 1;
                    2'd3: begin
                        i2c_scl <= 0;
                        if (bit_cnt == 0) begin
                            sda_en <= 0;
                            state <= ACK_CHECK;
                            if (config_write) saved_state <= WR_REG_DATA; 
                            else saved_state <= RESTART; // If reading, we Restart now
                            phase <= 0;
                        end else bit_cnt <= bit_cnt - 1;
                    end
                endcase
            end

            WR_REG_DATA: begin
                case (phase)
                    2'd0: sda_out <= REG_PWR_DATA[bit_cnt]; // Writes 0x01 to 0x06
                    2'd1: i2c_scl <= 1;
                    2'd3: begin
                        i2c_scl <= 0;
                        if (bit_cnt == 0) begin
                            sda_en <= 0; 
                            state <= ACK_CHECK;
                            saved_state <= STOP; 
                            phase <= 0;
                        end else bit_cnt <= bit_cnt - 1;
                    end
                endcase
            end

            ACK_CHECK: begin
                case (phase)
                    2'd0: i2c_scl <= 0;
                    2'd1: i2c_scl <= 1;
                    2'd3: begin
                        i2c_scl <= 0;
                        sda_en <= 1;    
                        phase <= 0;
                        state <= saved_state; 
                        
                        // Reset counters based on destination
                        if (saved_state == READ_DATA) begin
                            bit_cnt <= 7;
                            byte_cnt <= 0;
                            sda_en <= 0; 
                        end
                        else begin
                            bit_cnt <= 7; 
                        end
                    end
                endcase
            end

            STOP: begin
                case (phase)
                    2'd0: begin sda_out <= 0; i2c_scl <= 0; end
                    2'd1: i2c_scl <= 1;
                    2'd2: sda_out <= 1;
                    2'd3: begin
                        if (config_write) begin
                            config_done <= 1;
                            state <= WAIT_CONFIG; 
                        end else begin
                            accel_x <= {rx_data[0], rx_data[1]};
                            accel_y <= {rx_data[2], rx_data[3]};
                            accel_z <= {rx_data[4], rx_data[5]};
                            state   <= IDLE; 
                        end
                        phase <= 0;
                    end
                endcase
            end

            WAIT_CONFIG: begin
                if (refresh_timer < 20'd10000) refresh_timer <= refresh_timer + 1;
                else begin refresh_timer <= 0; state <= IDLE; end
            end

            RESTART: begin
                case (phase)
                    2'd0: begin sda_out <= 1; i2c_scl <= 0; end
                    2'd1: i2c_scl <= 1;
                    2'd2: sda_out <= 0; // Repeated Start
                    2'd3: begin 
                        i2c_scl <= 0; 
                        bit_cnt <= 7; 
                        state <= RD_DEV_ADDR; 
                        phase <= 0; 
                    end
                endcase
            end

            RD_DEV_ADDR: begin
                case (phase)
                    2'd0: begin 
                        // NOW we send the Read bit (1)
                        if (bit_cnt == 0) sda_out <= 1; 
                        else sda_out <= SLAVE_ADDR[bit_cnt-1]; 
                    end
                    2'd1: i2c_scl <= 1;
                    2'd3: begin 
                        i2c_scl <= 0;
                        if (bit_cnt == 0) begin 
                           sda_en <= 0; 
                           state <= ACK_CHECK;
                           saved_state <= READ_DATA;
                           phase <= 0; 
                        end 
                        else bit_cnt <= bit_cnt - 1;
                    end
                endcase
            end

            READ_DATA: begin
                case (phase)
                    2'd0: begin i2c_scl <= 0; sda_en <= 0; end
                    2'd1: i2c_scl <= 1;
                    2'd2: data_buffer[bit_cnt] <= i2c_sda;
                    2'd3: begin 
                        i2c_scl <= 0;
                        if (bit_cnt == 0) begin 
                            rx_data[byte_cnt] <= data_buffer; 
                            sda_en <= 1; 
                            // ACK for first 5 bytes, NACK for last byte (5)
                            sda_out <= (byte_cnt == 5) ? 1'b1 : 1'b0; 
                            state <= ACK_SEND; 
                            phase <= 0; 
                        end else bit_cnt <= bit_cnt - 1;
                    end
                endcase
            end

            ACK_SEND: begin
                case (phase)
                    2'd0: i2c_scl <= 0;
                    2'd1: i2c_scl <= 1;
                    2'd3: begin 
                        i2c_scl <= 0;
                        if (byte_cnt == 5) state <= STOP; 
                        else begin 
                            byte_cnt <= byte_cnt + 1; 
                            bit_cnt <= 7; 
                            sda_en <= 0; 
                            state <= READ_DATA; 
                        end
                        phase <= 0;
                    end
                endcase
            end

            default: state <= IDLE;
            endcase
        end
    end

endmodule