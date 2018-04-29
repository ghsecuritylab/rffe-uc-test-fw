/** MBED headers */
#include "mbed.h"
#include "PinNames.h"
#include "rtos.h"
#include "EthernetInterface.h"
#include "TCPServer.h"
#include "TCPSocket.h"
#include "lpc_phy.h"
#include "CDCE906.h"

#define DP8_SPEED10MBPS (1 << 1)    /**< 1=10MBps speed */
#define DP8_VALID_LINK  (1 << 0)    /**< 1=Link active */

#define BUFSIZE         256
#define SERVER_PORT     6791

/* MBED Reset function */
extern "C" void mbed_reset();

// Hardware Initialization - MBED
char ip[4],mask[4],gateway[4],mac[6];

extern "C" void mbed_mac_address(char *s) {
    // Write your code to get the MAC address from the 25AA02E48 into mac[6]
    memcpy(s, mac, 6);
}


// MBED pins
RawSerial pc(P0_2, P0_3); // Serial USB port. (NOTE: All printf() calls are redirected to this port)
DigitalOut led1(P1_18);
DigitalOut led2(P1_20);
DigitalOut led3(P1_21);
DigitalOut led4(P1_23);
DigitalIn sw1(P1_29);
DigitalIn sw2(P2_11);
DigitalIn sw3(P2_12);
DigitalIn sw4(P2_13);

bool get_eth_link_status(void)
{
    return (lpc_mii_read_data() & DP8_VALID_LINK) ? true : false;
}

int loopback_check( PinName n0, PinName n1 )
{
    DigitalIn *in;
    DigitalOut *out;

    in = new DigitalIn(n0, PullNone);
    if (n0 == P0_29) {
        DigitalIn usb_in(P0_30);
    }
    if (n0 == P0_30) {
        DigitalIn usb_in(P0_29);
    }
    out = new DigitalOut(n1);

    *out = 1;
    if (*in != 1) {
        return 1;
    }

    *out = 0;
    if (*in != 0) {
        return 1;
    }

    delete in;
    delete out;

    /* Reverse direction */
    in = new DigitalIn(n1, PullNone);
    out = new DigitalOut(n0);
    if (n0 == P0_29) {
        DigitalOut usb_out(P0_30);
    }
    if (n0 == P0_30) {
        DigitalOut usb_out(P0_29);
    }

    *out = 1;
    if (*in != 1) {
        return 1;
    }

    *out = 0;
    if (*in != 0) {
        return 1;
    }

    delete in;
    delete out;

    return 0;
}

int GPIO_loopback_test( void )
{
    uint8_t err = 0;
    uint8_t t;
    PinName loop_pair[9][2] = {
        {p8,p14},
        {p9,p10},
        {p17,p18},
        {p19,p20},
        {p21,p22},
        {p23,p24},
        {p25,p26},
        {P0_29,p27},
        {P0_30,p28}
    };

    printf("\nStarting GPIO Loopback test\n\r");

    for( t = 0; t < sizeof(loop_pair)/sizeof(loop_pair[0]); t++) {
        printf("GPIO Loopback pair [%s][%s]:", pin_name_str[loop_pair[t][0]].c_str(), pin_name_str[loop_pair[t][1]].c_str() );
        if (loopback_check(loop_pair[t][0], loop_pair[t][1]) == 0) {
            printf("\tPass!\n\r");
        } else {
            err = 1;
            printf("\tFail!\n\r");
        }
    }

    printf("\n\r");
    return err;
}

#define PS_TEST_3V3_NOMINAL   (3.3)
#define PS_TEST_3V3_TOLERANCE (PS_TEST_3V3_NOMINAL*0.1)
#define PS_TEST_3V3_LOW       (PS_TEST_3V3_NOMINAL-PS_TEST_3V3_TOLERANCE)
#define PS_TEST_3V3_HIGH      (PS_TEST_3V3_NOMINAL+PS_TEST_3V3_TOLERANCE)

#define PS_TEST_5V_NOMINAL    (5.0)
#define PS_TEST_5V_TOLERANCE  (PS_TEST_5V_NOMINAL*0.1)
#define PS_TEST_5V_LOW        (PS_TEST_5V_NOMINAL-PS_TEST_5V_TOLERANCE)
#define PS_TEST_5V_HIGH       (PS_TEST_5V_NOMINAL+PS_TEST_5V_TOLERANCE)

int power_supply_test( void )
{
    uint8_t err = 0;
    AnalogIn adc_3v3(p15);
    AnalogIn adc_5v(p16);
    float adc_read = 0;

    printf("\nStarting Power Supply level test\n\r");

    adc_read = adc_3v3.read()*3.3*2;
    printf("Power Supply 3.3v read: %.4f", adc_read);
    if ( (adc_read <= PS_TEST_3V3_HIGH) && (adc_read >= PS_TEST_3V3_LOW) ) {
        printf("\tPass!\n\r");
    } else {
        printf("\tFail!\n\r");
        err = 1;
    }

    adc_read = adc_5v.read()*3.3*2;
    printf("Power Supply 5.0v read: %.4f", adc_read);
    if ( (adc_read <= PS_TEST_5V_HIGH) && (adc_read >= PS_TEST_5V_LOW) ) {
        printf("\tPass!\n\r");
    } else {
        printf("\tFail!\n\r");
        err = 1;
    }

    return err;
}

unsigned int random( void )
{
    unsigned int x = 0;
    unsigned int iRandom = 0;

    AnalogIn analog(p15);

    for (x = 0; x <= 32; x += 2)
    {
        iRandom += ((analog.read_u16() % 3) << x);
        wait_us (10);
    }

    return iRandom;
}

int feram_test( void )
{
    I2C feram_i2c( P0_19, P0_20 );

    DigitalOut feram_wp( P0_21 );

    feram_wp = 0;

    const uint8_t slave_id = 0xA;
    uint8_t page, addr;
    uint16_t byte;
    uint8_t err = 1;
    uint16_t err_write = 0, err_read = 0;
    uint8_t data[2];
    uint8_t test_pattern[256];

    /* Fill test pattern array with random numbers */
    printf("FeRAM Random pattern:\n\r");
    for (byte = 0; byte < sizeof(test_pattern); byte++) {
        if ((byte%8) == 0) {
            printf("\n\r[RANDOM]");
        }
        test_pattern[byte] = random()%256;
        printf("%02X ", test_pattern[byte]);
    }

    printf("\n\rStarting FeRAM test\n\r");

    printf("Writing test pattern...");
    /* Write test pattern on the FeRAM */
    for (page = 0; page <= 7; page++) {
        for (byte = 0; byte <= 0xFF; byte++) {
            addr = (slave_id << 4) | (page << 1);
            data[0] = byte;
            data[1] = test_pattern[byte];
            err_write += feram_i2c.write(addr, (char *)data, 2);
        }
    }
    if (err_write == 0) {
        printf("\tPass!\n\r");
        err = 0;
    } else {
        printf("\tFail!\n\r");
        return 1;
    }

    printf("Reading FeRAM pattern...\n\r");
    /* Check test pattern */
    for (page = 0; page <= 7; page++) {
        for (byte = 0; byte <= 0xFF; byte++) {
            addr = (slave_id << 4) | (page << 1);
            data[0] = byte;
            feram_i2c.write(addr, (char *)data, 1);
            err_read += feram_i2c.read(addr, (char *)data, 1);

            if (data[0] != test_pattern[byte]) {
                err_read++;
            }
        }
    }
    printf("[FERAM]");
    if (err_read == 0) {
        printf("\tPass!\n\r");
        err = 0;
    } else {
        printf("\tFail!\n\r");
        return 1;
    }


    /* Zero-write the FeRAM */
    memset(test_pattern, 0, 256);
    for (page = 0; page <= 7; page++) {
        for (byte = 0; byte <= 0xFF; byte++) {
            addr = (slave_id << 4) | (page << 1);
            data[0] = byte;
            data[1] = test_pattern[byte];
            feram_i2c.write(addr, (char *)data, 2);
        }
    }

    return err;
}

uint8_t feram_store_eth_info( void )
{
    I2C feram_i2c( P0_19, P0_20 );

    DigitalOut feram_wp( P0_21 );

    feram_wp = 0;

    const uint8_t slave_id = 0xA;
    uint8_t addr;
    uint16_t byte;
    uint8_t err = 0;
    uint8_t data[2];

    /* Read addresses from UART */
    printf("Insert MAC:\r\n");
    scanf("%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",&mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]);
    for (uint8_t t = 0; t < 6; t++) {
        printf("%02X", mac[t]);
        if (t != 5) {
            printf(":");
        }
    }
    led1 = 1;

    printf("\n\rInsert IP:\r\n");
    scanf("%hhd.%hhd.%hhd.%hhd",&ip[0],&ip[1],&ip[2],&ip[3]);
    for (uint8_t t = 0; t < 4; t++) {
        printf("%d", ip[t]);
        if (t != 3) {
            printf(".");
        }
    }
    led2 = 1;

    printf("\n\rInsert Mask:\r\n");
    scanf("%hhd.%hhd.%hhd.%hhd",&mask[0],&mask[1],&mask[2],&mask[3]);
    for (uint8_t t = 0; t < 4; t++) {
        printf("%d", mask[t]);
        if (t != 3) {
            printf(".");
        }
    }
    led3 = 1;

    printf("\n\rInsert Gateway:\r\n");
    scanf("%hhd.%hhd.%hhd.%hhd",&gateway[0],&gateway[1],&gateway[2],&gateway[3]);
    for (uint8_t t = 0; t < 4; t++) {
        printf("%d", gateway[t]);
        if (t != 3) {
            printf(".");
        }
    }
    led4 = 1;
    printf("\n\r");

    printf("\n\r Writing info to FeRAM...\n\r");

    for (byte = 0x0; byte < sizeof(mac); byte++) {
        addr = (slave_id << 4);
        data[0] = byte;
        data[1] = mac[byte];
        err += feram_i2c.write(addr, (char *)data, 2);
    }

    for (byte = 0x0; byte < sizeof(ip); byte++) {
        addr = (slave_id << 4);
        data[0] = byte+0x10;
        data[1] = ip[byte];
        err += feram_i2c.write(addr, (char *)data, 2);
    }

    for (byte = 0x0; byte < sizeof(mask); byte++) {
        addr = (slave_id << 4);
        data[0] = byte+0x20;
        data[1] = mask[byte];
        err += feram_i2c.write(addr, (char *)data, 2);
    }

    for (byte = 0x0; byte < sizeof(gateway); byte++) {
        addr = (slave_id << 4);
        data[0] = byte+0x30;
        data[1] = gateway[byte];
        err += feram_i2c.write(addr, (char *)data, 2);
    }

    return err;
}

#define LDR_LIGHT_THRESHOLD 2.9

int leds_test( void )
{
    int err = 0;
    AnalogIn ldr(p18);
    DigitalOut ldr_loop(p17);
    ldr_loop = 1;

    PinName leds[4] = {
        P1_18,
        P1_20,
        P1_21,
        P1_23,
    };

    DigitalOut *led;

    /* Turn off all the LEDs */
    for( uint8_t t = 0; t < sizeof(leds)/sizeof(leds[0]); t++) {
        led = new DigitalOut(leds[t]);
        *led = 0;
        delete led;
    }

    printf("Starting LEDs test...\n\r");

    for( uint8_t t = 0; t < sizeof(leds)/sizeof(leds[0]); t++) {
        led = new DigitalOut(leds[t]);

        *led = 1;
        Thread::wait(500);
        float ldr_read = ldr.read()*3.3;

        printf("[LED] %d: %.4f", t+1, ldr_read);
        if (ldr_read < LDR_LIGHT_THRESHOLD) {
            printf("\tPass!\n\r");
        } else {
            printf("\tFail!\n\r");
            err = 1;
        }

        *led = 0;
        delete led;
    }

    return err;
}

void pll_cfg( void )
{
    I2C pll_i2c(P0_27, P0_28);

    CDCE906 pll( pll_i2c, 0b11010010 );

    pll.cfg_eth();
}

int ethernet_test( void )
{
    int err=0;
    // Ethernet initialization
    EthernetInterface net;
    TCPSocket client;
    SocketAddress client_addr;
    TCPServer server;

    const char msg[] = "Test msg!";
    uint8_t buf[10];
    uint8_t t = 0;
    uint8_t recv_sz = 0;

    printf("\n\rStarting Ethernet test\n\r");

    printf("Configuring 50MHz PLL\n\r");
    pll_cfg();

    /* Read addresses from UART */
    printf("Insert MAC:\r\n");
    scanf("%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",&mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]);
    for (uint8_t t = 0; t < 6; t++) {
        printf("%02X", mac[t]);
        if (t != 5) {
            printf(":");
        }
    }
    led1 = 1;

    printf("\n\rInsert IP:\r\n");
    scanf("%hhd.%hhd.%hhd.%hhd",&ip[0],&ip[1],&ip[2],&ip[3]);
    for (uint8_t t = 0; t < 4; t++) {
        printf("%d", ip[t]);
        if (t != 3) {
            printf(".");
        }
    }
    led2 = 1;

    printf("\n\rInsert Mask:\r\n");
    scanf("%hhd.%hhd.%hhd.%hhd",&mask[0],&mask[1],&mask[2],&mask[3]);
    for (uint8_t t = 0; t < 4; t++) {
        printf("%d", mask[t]);
        if (t != 3) {
            printf(".");
        }
    }
    led3 = 1;

    printf("\n\rInsert Gateway:\r\n");
    scanf("%hhd.%hhd.%hhd.%hhd",&gateway[0],&gateway[1],&gateway[2],&gateway[3]);
    for (uint8_t t = 0; t < 4; t++) {
        printf("%d", gateway[t]);
        if (t != 3) {
            printf(".");
        }
    }
    led4 = 1;
    printf("\n\r");

    char ip_str[16], gateway_str[16], mask_str[16];

    sprintf(ip_str, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    sprintf(gateway_str, "%d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
    sprintf(mask_str, "%d.%d.%d.%d", mask[0], mask[1], mask[2], mask[3]);

    err = net.set_network(ip_str,mask_str,gateway_str);

    printf("Initializing ETH stack...");

    do {
        err = net.connect();
        t++;
    } while ( (err != 0) && (t <= 5) );

    if ( err == 0 ) {
        printf("\tPass!\n\r");
    } else {
        printf("\tFail!\n\r");
        return 1;
    }

    printf("IP: %s\n\r", net.get_ip_address());
    printf("RFFE MAC Address: %s\n\r", net.get_mac_address());
    printf("Listening on port: %d\n\r", SERVER_PORT);

    /* Bind tcp server */
    server.open(&net);
    server.bind(net.get_ip_address(), SERVER_PORT);
    server.listen();

    client.set_blocking(true);
    server.accept(&client, &client_addr);

    recv_sz = client.recv(&buf[0], sizeof(msg));
    err = 1;
    if (recv_sz > 0 ) {
        printf("Received: \"%s\"", buf);
        if( strcmp((char *)msg, (char *)buf) == 0 ) {
            printf("\tPass!\n\r");
            err = 0;
        } else {
            /* Add terminating char to prevent gargabe print */
            buf[9] = 0;
            printf("\tFail!\n\r");
            /* clear buffer */
            memset(buf, 0, sizeof(buf));
        }
    } else {
        printf("Client Disconnected!\n");
    }

    client.close();

    return err;
}

Ticker blink;

static void blink_callback( void )
{
    led1 = !led1;
    led2 = !led2;
    led3 = !led3;
    led4 = !led4;
}

int main( void )
{
    //Init serial port for info printf
    pc.baud(115200);

    printf("Starting rffe-uc tests!\n\r");
    printf("Send the char 's' to start the tests or 'r' to store the deploy information in the FERAM!\n\r");

    int err = 0;

    char t = 0;

    while( t != 's' && t != 'r') {
        t = pc.getc();
    }

    if (t == 's') {
        err += ((feram_test() & 1) << 0);
        err += ((GPIO_loopback_test() & 1) << 1);
        err += ((power_supply_test() & 1) << 2);
        err += ((leds_test() & 1) << 3);
        err += ((ethernet_test() & 1) << 4);
    } else if (t == 'r') {
        err += feram_store_eth_info();
    }

    printf("\n\r");

    if (err == 0) {
        printf("Board tests passed!\n\r");
        blink.attach(&blink_callback, 0.5);
    } else {
        printf("Board tests failed!\n\r");
    }

    printf("\n\rEnd of tests!\n\r");

    while(1);
}
