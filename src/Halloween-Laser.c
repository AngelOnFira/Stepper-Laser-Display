#include "Halloween-Laser.h"

PicoStepper devices[2];
int positions[2];

PicoStepper YAxis;
PicoStepper XAxis;

volatile int dma_chan;
volatile PIO pio;
volatile uint sm;

volatile bool xfrReceived = false;
volatile uint8_t buffer_id = 0;
volatile uint32_t buffer_one[TRANSFER_SIZE];
volatile uint32_t buffer_two[TRANSFER_SIZE];

bool checksum(uint message){

    uint sum = message ^ (message >> 1);
    sum = sum ^ (sum >> 2);
    sum = sum ^ (sum >> 4);
    sum = sum ^ (sum >> 8);
    sum = sum ^ (sum >> 16);
    
    sum = sum & 1;

    return sum;
}

uint16_t retrieve(uint32_t frame, uint32_t mask, uint32_t shift){
    return (frame & mask) >> shift;
}

void draw(){

    volatile uint32_t *buffer;

    buffer = buffer_id ? buffer_two : buffer_one;

    if(DEBUG) sleep_ms(2000);

    if(checksum(buffer[0])) return;
    if(retrieve(buffer[0], ID_MASK, ID_SHIFT) != PROJECTOR_ID) return;
    if(!retrieve(buffer[0], ENABLE_MASK, ENABLE_SHIFT)){
        gpio_put(ENABLE, false ^ DRIVER);
        return;
    } else gpio_put(ENABLE, true ^ DRIVER);
    if(retrieve(buffer[0], HOME_MASK, HOME_SHIFT)){
        if(DEBUG) printf("Homing\n");
        home_steppers();
        buffer[0] = buffer[0] ^ HOME_MASK;
        if(DEBUG) printf("Homing complete\n");
        set_stepper_values();
    }

    uint8_t pointCount = retrieve(buffer[0], COUNT_MASK, COUNT_SHIFT);
    xfrReceived = false;

    if(DEBUG){
        printf("Point count: %d\n", pointCount);
        printf("xfrReceived: %d\n", xfrReceived);
    }

    while(true){

        if(xfrReceived) break;

        for(uint8_t idx = 0; idx < pointCount; idx++){

            bool sum = checksum(buffer[idx+1]);

            uint16_t xPos = retrieve(buffer[idx+1], X_MASK, X_SHIFT);
            uint16_t yPos = retrieve(buffer[idx+1], Y_MASK, Y_SHIFT);
            uint8_t red = retrieve(buffer[idx+1], RED_MASK, RED_SHIFT);
            uint8_t green = retrieve(buffer[idx+1], GREEN_MASK, GREEN_SHIFT);
            uint8_t blue = retrieve(buffer[idx+1], BLUE_MASK, BLUE_SHIFT);

            if(sum){
                lasers_off();
                if(DEBUG) printf("Skipping point\n");
                continue;
            }

            set_red_pwm(red);
            set_green_pwm(green);
            set_blue_pwm(blue);

            positions[0] = xPos > MAX_X ? MAX_X : xPos;
            positions[1] = yPos > MAX_Y ? MAX_Y : yPos;
            picostepper_move_to_positions(devices, positions, 2);

            sleep_us(1);

            if(DEBUG) printf("R: %X\nG: %X\nB: %X\nX: %X\nY: %X\n", red, green, blue, xPos, yPos);

            if(DEBUG) sleep_ms(3000);

        }
    }

    lasers_off();
    xfrReceived = false;

}

void lasers_off(){
    set_red_pwm(0);
    set_green_pwm(0);
    set_blue_pwm(0);
}

void set_red_pwm(uint8_t pwm){
    pwm_set_chan_level(RED_SLICE, RED_CHANNEL, PWM_DEPTH - ((pwm % COLOUR_DEPTH) * COLOUR_MULTIPLIER));
}

void set_green_pwm(uint8_t pwm){
    pwm_set_chan_level(GREEN_SLICE, GREEN_CHANNEL, PWM_DEPTH - ((pwm % COLOUR_DEPTH) * COLOUR_MULTIPLIER));
}

void set_blue_pwm(uint8_t pwm){
    pwm_set_chan_level(BLUE_SLICE, BLUE_CHANNEL, PWM_DEPTH - ((pwm % COLOUR_DEPTH) * COLOUR_MULTIPLIER));
}

void homing_sequence(PicoStepper device){
    if(picostepper_get_async_enabled(device)) picostepper_move_async(device, 100, &homing_sequence);
}

void slow_home(PicoStepper device){
    if(picostepper_get_async_enabled(device)) picostepper_move_async(device, 10, &homing_sequence);
}

void init_gpio(){
    gpio_init(HOME_X);
    gpio_init(HOME_Y);
    gpio_init(RED);
    gpio_init(GREEN);
    gpio_init(BLUE);
    gpio_init(ENABLE);

    gpio_set_dir(HOME_X, false);
    gpio_set_dir(HOME_Y, false);
    gpio_set_dir(ENABLE, true);

    gpio_set_function(RED, GPIO_FUNC_PWM);
    gpio_set_function(GREEN, GPIO_FUNC_PWM);
    gpio_set_function(BLUE, GPIO_FUNC_PWM);

    gpio_pull_up(HOME_X);
    gpio_pull_up(HOME_Y);

    gpio_pull_down(CLOCK);
    gpio_pull_down(DATA);

    gpio_put(ENABLE, true ^ DRIVER);
}

void init_steppers(){

    YAxis = picostepper_pindef_init(DIR_Y, STEP_Y, TwoWireDriver);
    XAxis = picostepper_pindef_init(DIR_X, STEP_X, TwoWireDriver);

    devices[0] = XAxis;
    devices[1] = YAxis;

}

void set_stepper_values(){
    picostepper_set_async_enabled(XAxis, true);
    picostepper_set_max_speed(XAxis, MAXSPEED);
    picostepper_set_min_speed(XAxis, MINSPEED);

    picostepper_set_async_enabled(YAxis, true);
    picostepper_set_max_speed(YAxis, MAXSPEED);
    picostepper_set_min_speed(YAxis, MINSPEED);

    picostepper_set_acceleration(XAxis, ACCELERATION);
    picostepper_set_acceleration(YAxis, ACCELERATION);
}

void home_steppers(){
    picostepper_set_async_speed(XAxis, HOMINGSPEED);
    picostepper_set_acceleration(XAxis, 0);
    picostepper_set_async_enabled(XAxis, true);

    picostepper_set_async_direction(XAxis, true);

    picostepper_move_async(XAxis, 100, &homing_sequence);
    
    while(!gpio_get(HOME_X));

    picostepper_set_async_enabled(XAxis, false);

    sleep_ms(500);

    picostepper_set_async_enabled(XAxis, true);

    picostepper_set_async_direction(XAxis, false);

    picostepper_move_async(XAxis, 100, &homing_sequence);
    
    while(gpio_get(HOME_X));

    picostepper_set_async_enabled(XAxis, false);

    sleep_ms(500);

    picostepper_set_async_enabled(XAxis, true);
    picostepper_move_blocking(XAxis, 500, true, picostepper_convert_speed_to_delay(HOMINGSPEED), 0);

    picostepper_set_async_direction(XAxis, false);
    picostepper_set_async_speed(XAxis, SLOWHOMESPEED);

    picostepper_move_async(XAxis, 10, &slow_home);
    
    while(gpio_get(HOME_X));

    picostepper_set_async_enabled(XAxis, false);

    sleep_ms(500);

    picostepper_set_async_speed(YAxis, HOMINGSPEED);
    picostepper_set_acceleration(YAxis, 0);
    picostepper_set_async_enabled(YAxis, true);

    picostepper_set_async_direction(YAxis, true);

    picostepper_move_async(YAxis, 100, &homing_sequence);
    
    while(!gpio_get(HOME_Y));

    picostepper_set_async_enabled(YAxis, false);

    sleep_ms(500);

    picostepper_set_async_enabled(YAxis, true);

    picostepper_set_async_direction(YAxis, false);

    picostepper_move_async(YAxis, 100, &homing_sequence);
    
    while(gpio_get(HOME_Y));

    picostepper_set_async_enabled(YAxis, false);

    sleep_ms(500);

    picostepper_set_async_enabled(YAxis, true);
    picostepper_move_blocking(YAxis, 500, true, picostepper_convert_speed_to_delay(HOMINGSPEED), 0);

    picostepper_set_async_direction(YAxis, false);
    picostepper_set_async_speed(YAxis, SLOWHOMESPEED);

    picostepper_move_async(YAxis, 10, &slow_home);
    
    while(gpio_get(HOME_Y));

    picostepper_set_async_enabled(YAxis, false);

    sleep_ms(500);

    picostepper_set_async_enabled(XAxis, true);
    picostepper_move_blocking(XAxis, X_HOME_POS, true, picostepper_convert_speed_to_delay(HOMINGSPEED), 0);

    picostepper_set_async_enabled(YAxis, true);
    picostepper_move_blocking(YAxis, Y_HOME_POS, true, picostepper_convert_speed_to_delay(HOMINGSPEED), 0);

    picostepper_set_async_enabled(XAxis, true);
    picostepper_set_position(XAxis, 0);
    picostepper_set_async_enabled(YAxis, true);
    picostepper_set_position(YAxis, 0);

}

void set_home(){

    getchar();

    while(true){

        sleep_ms(1000);

        uint pos_x = 0;
        uint pos_y = 0;

        for(uint i=0; i<10; i++){
            uint input = getchar();
            if(input == 13) break;
            pos_x = pos_x*10 + input-48;
        }

        for(uint i=0; i<10; i++){
            uint input = getchar();
            if(input == 13) break;
            pos_y = pos_y*10 + input-48;
        }

        if(pos_x == 420 && pos_y == 69){
            printf("Exiting!\n");
            return;
        }

        if(pos_x > 2000 || pos_y > 2000){
            printf("Erroneous position detected!\n");
            continue;
        }

        printf("Positions updated to X: %d Y: %d\n", pos_x, pos_y);

        positions[0] = pos_x;
        positions[1] = pos_y;

        picostepper_move_to_positions(devices, positions, 2);

    }

}

volatile bool first = true;

void dma_handler(){

    if(true){

        volatile uint32_t *buffer;
        buffer = buffer_id ? buffer_one : buffer_two;

        printf("Checksums: ");
        for(uint i=0; i<TRANSFER_SIZE; i++){
            if(checksum(buffer[i])) printf("Invalid ");
            else printf("Valid ");
        }
        printf("\n");

        printf("Received: ");
        for(uint i=0; i<TRANSFER_SIZE; i++){
            printf("%X ", buffer[i]);
        }
        printf("\n");

        //draw();
    }

    // Clear the interrupt request.
    dma_hw->ints0 = 1u << dma_chan;
    // re-trigger it
    if(buffer_id){
        buffer_id = 0;
        dma_channel_set_read_addr(dma_chan, &pio->rxf[sm], false);
        dma_channel_set_write_addr(dma_chan, buffer_two, true);
    } else {
        buffer_id = 1;
        dma_channel_set_read_addr(dma_chan, &pio->rxf[sm], false);
        dma_channel_set_write_addr(dma_chan, buffer_one, true);
    }

    xfrReceived = true;

    if(DEBUG) printf("Received: %d\n", xfrReceived);

    if(DEBUG) printf("Interrupt cleared\n");
}

void serialReceiver(){

    printf("Initializing Core 1...\n");

    pwm_set_clkdiv(PWM_SLICE_ONE, PWM_CLOCK_DIV);
    pwm_set_wrap(PWM_SLICE_ONE, PWM_DEPTH); 
    pwm_set_enabled(PWM_SLICE_ONE, true);

    pwm_set_clkdiv(PWM_SLICE_TWO, PWM_CLOCK_DIV);
    pwm_set_wrap(PWM_SLICE_TWO, PWM_DEPTH); 
    pwm_set_enabled(PWM_SLICE_TWO, true);

    printf("PWM configured\n");

    pio = pio1;
    uint offset = pio_add_program(pio, &clocked_input_program);
    sm = pio_claim_unused_sm(pio, true);

    printf("PIO allocated\n");

    pio_sm_clear_fifos(pio, sm);
    clocked_input_program_init(pio, sm, offset, DATA);

    printf("PIO clocked input configured\n");

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_PIO1_RX0);

    printf("DMA initialized\n");

    dma_channel_configure(
        dma_chan,
        &c,
        buffer_one, // Write address (only need to set this once)
        NULL,             // Don't provide a read address yet
        TRANSFER_SIZE, // Write the same value many times, then halt and interrupt
        false             // Don't start yet
    );

    printf("DMA configured\n");

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq1_enabled(dma_chan, true);

    printf("DMA interrupt configured\n");

    // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
    irq_set_exclusive_handler(DMA_IRQ_1, dma_handler);

    printf("DMA interrupt handler configured\n");

    irq_set_enabled(DMA_IRQ_1, true);

    printf("DMA interrupts started\n");

    pio_sm_clear_fifos(pio, sm);

    printf("FIFO cleared\n");

    // Manually call the handler once, to trigger the first transfer
    dma_handler();

    if(DEBUG) printf("DMA handler initialized\n");

    while(true){

        sleep_ms(100);

    }

    printf("Error\n");
}

int main() {

    stdio_init_all();
    sleep_ms(3000);
    printf("Initializing...\n");
    sleep_ms(5000);
    printf("%d\n", NUMSTEPS);

    init_steppers();
    printf("Steppers intialized\n");
    set_stepper_values();
    printf("Steppers configured\n");
    init_gpio();
    printf("GPIO initialized\n");

    home_steppers();
    printf("Steppers homed\n");
    set_stepper_values();
    printf("Steppers configured\n");

    //set_home();

    multicore_launch_core1(serialReceiver);

    sleep_ms(5000);

    while(true){

        if(!DEBUG){
            draw();
        }
        sleep_ms(100);

        /*
        positions[0] = MAX_X;
        positions[1] = 0;
        gpio_put(BLUE, false);
        picostepper_move_to_positions(devices, positions, 2);
        //sleep_ms(1000);

        positions[0] = MAX_X;
        positions[1] = MAX_Y;
        gpio_put(BLUE, true);
        picostepper_move_to_positions(devices, positions, 2);
        //sleep_ms(1000);

        positions[0] = 0;
        positions[1] = MAX_Y;
        gpio_put(BLUE, false);
        picostepper_move_to_positions(devices, positions, 2);
        //sleep_ms(1000);

        positions[0] = 0;
        positions[1] = 0;
        gpio_put(BLUE, true);
        picostepper_move_to_positions(devices, positions, 2);
        //sleep_ms(1000);
        */
    }

  return -1;
}