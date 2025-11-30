module pwm_generator (
    input  wire       clk,
    input  wire       reset_n,
    input  wire [7:0] duty_cycle,   // 0–100%
    output reg        pwm_out
);

    localparam PERIOD = 16'd50000;  // 1 kHz PWM at 50 MHz

    reg [15:0] counter;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n)
            counter <= 0;
        else if (counter >= PERIOD)
            counter <= 0;
        else
            counter <= counter + 1;
    end

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n)
            pwm_out <= 0;
        else begin
            if (counter < (PERIOD * duty_cycle) / 100)
                pwm_out <= 1;
            else
                pwm_out <= 0;
        end
    end

endmodule
