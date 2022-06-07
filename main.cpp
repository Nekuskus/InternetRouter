#include <iostream>
#include <wiringPi.h>
#include <thread>
#include <queue>

#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>



#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */

int  mem_fd;
void *gpio_map;

volatile unsigned *gpio;

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))


#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock

//
// Set up a memory regions to access GPIO
//
void setup_io()
{
   /* open /dev/mem */
    if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
        printf("can't open /dev/mem \n");
        exit(-1);
    }

   /* mmap GPIO */
    gpio_map = mmap(
        NULL,             //Any adddress in our space will do
        BLOCK_SIZE,       //Map length
        PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
        MAP_SHARED,       //Shared with other processes
        mem_fd,           //File to map
        GPIO_BASE         //Offset to GPIO peripheral
    );

    close(mem_fd); //No need to keep mem_fd open after mmap

    if (gpio_map == MAP_FAILED) {
        printf("mmap error %d\n", (int)gpio_map);//errno also set!
        exit(-1);
    }

   // Always use volatile pointer!
   gpio = (volatile unsigned *)gpio_map;


} // setup_io

char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

unsigned char macaddress[6] = {0b10000101, 0b10010000, 0b00000000, 0b00000000, 0b00000000, 0x3};

// there isn't a second router yet, so there's no need to implement a gateway here!
// uint32_t gatewayaddress;
// uint32_t gatewaymask;


//String protocols[3] = {"DHCP", "ICMP", "SEND"};
std::string protocols[3] = {"DHCP", "ICMP", "SEND"};

class L7Data {
public:
    unsigned char data9;
    unsigned char data8;
    unsigned char data7;
    unsigned char data6;
    unsigned char data5;
    unsigned char data4;
    unsigned char data3;
    unsigned char data2;
    unsigned char data1;
    unsigned char data0;
    L7Data(unsigned char _data[10]) : data0{_data[0]}, data1{_data[1]}, data2{_data[2]}, data3{_data[3]}, data4{_data[4]}, data5{_data[5]}, data6{_data[6]}, data7{_data[7]}, data8{_data[8]}, data9{_data[9]} {
      //memcpy((void*) data, (const void *) _data, sizeof(data));
    }
    L7Data& operator=(const L7Data&) = default;
};
class L6Data {
public:
    unsigned char protocolid;
    //0 DHCP
    //1 ICMP
    //2 SEND
    L7Data L7;
    L6Data(unsigned char _protocolid, L7Data l7) : protocolid{_protocolid}, L7{l7} {

    }
    L6Data& operator=(const L6Data&) = default;
};
class L5Session {
public:
    unsigned char sessionid;
    L6Data L6;
    L5Session(unsigned char _sessionid, L6Data l6) : sessionid{_sessionid}, L6{l6} {

    }
    L5Session& operator=(const L5Session&) = default;
};
class L4DatagramUDP {
public:
    bool istcp = false;
    unsigned char destinationport;
    unsigned char sourceport;
    // 1 = apka od przytrzymywania przycisku
    // dhcp: source=68, dest=67
    // 0-254 = adresy aplikacji, np w I2CCom
    L5Session L5;
    L4DatagramUDP(unsigned char _destinationport, unsigned char _sourceport, L5Session l5, bool _istcp = false) : destinationport{_destinationport}, sourceport{_sourceport}, L5{l5}, istcp{_istcp} {

    }
    L4DatagramUDP& operator=(const L4DatagramUDP&) = default;
};
class L3PacketUDP {
public:
    uint32_t destinationip;
    uint32_t sourceip;
    uint32_t sourcemask;
    unsigned char TTL;
    L4DatagramUDP L4;
    L3PacketUDP(uint32_t _destinationip, uint32_t _sourceip, uint32_t _sourcemask, unsigned char _TTL, L4DatagramUDP l4) : destinationip{_destinationip}, sourceip{_sourceip}, sourcemask{_sourcemask}, TTL{_TTL}, L4{l4} {

    }
    L3PacketUDP& operator=(const L3PacketUDP&) = default;
};
class L2FrameUDP {
public:
    unsigned char destinationmac5;
    unsigned char destinationmac4;
    unsigned char destinationmac3;
    unsigned char destinationmac2;
    unsigned char destinationmac1;
    unsigned char destinationmac0;
    unsigned char sourcemac5;
    unsigned char sourcemac4;
    unsigned char sourcemac3;
    unsigned char sourcemac2;
    unsigned char sourcemac1;
    unsigned char sourcemac0;
    L3PacketUDP L3;
    uint32_t crc;
    L2FrameUDP(unsigned char _destinationmac[6], unsigned char _sourcemac[6], L3PacketUDP l3, uint32_t _crc = 0) : sourcemac0{_sourcemac[0]}, sourcemac1{_sourcemac[1]}, sourcemac2{_sourcemac[2]}, sourcemac3{_sourcemac[3]}, sourcemac4{_sourcemac[4]}, sourcemac5{_sourcemac[5]}, destinationmac0{_destinationmac[0]}, destinationmac1{_destinationmac[1]}, destinationmac2{_destinationmac[2]}, destinationmac3{_destinationmac[3]}, destinationmac4{_destinationmac[4]}, destinationmac5{_destinationmac[5]}, L3{l3}, crc{_crc} {
      //memcpy((void *)&destinationmac, (const void *)&_destinationmac, sizeof(destinationmac));
      //memcpy((void *)&sourcemac, (const void *)&_sourcemac, sizeof(sourcemac));
    }
    L2FrameUDP& operator=(const L2FrameUDP&) = default;
};

// these are meant to transport data from the io thread to the routing one
// pair arguments: unsigned char - eth channel id (0-3), L2FrameUDP - the frame
std::queue<std::pair<unsigned char, L2FrameUDP>> outgoingdata;
std::queue<std::pair<unsigned char, L2FrameUDP>> incomingdata;

void setGPIO(int pin, int value) {
    GPIO_SET = (value << pin);
}

void routingthread() {

}

int main() {
    if(wiringPiSetupSys() == -1) {
        return 1;
    }

    int g, rep;
    setup_io();
    for(int i = 0; i < 24; i++) { // init pins 0-23
        // eth0: pins 0-5
        // eth1: pins 6-11
        // eth2: pins 12-17
        // eth3: pins 18-23
        
    }
    std::thread router(routingthread);

    /*
    for(int i = 0; i < 6; i++) {
        pinMode(i, OUTPUT);
        digitalWrite(i, 0);
    }
    delay(2500);
    for(int i = 0; i < 6; i++) {
        if(i % 2 == 0) digitalWrite(i, 1);
    }
    std::string halt;
    std::cin >> halt;
    */
}

