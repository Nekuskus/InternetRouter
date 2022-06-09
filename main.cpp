#include <iostream>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <thread>
#include <queue>
#include <bitset>
#include <cstring>
//#include <pi-gpio.h>

char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

unsigned char macaddress[6] = {0b10000101, 0b10010000, 0b00000000, 0b00000000, 0b00000000, 0x3};

// there isn't a second router yet, so there's no need to implement a gateway here!
// uint32_t gatewayaddress;
// uint32_t gatewaymask;


//String protocols[3] = {"DHCP", "ICMP", "SEND"};
std::string protocols[4] = {"DHCP", "ICMP", "SEND", "ARP"};

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
    unsigned char destinationmac0;
    unsigned char destinationmac1;
    unsigned char destinationmac2;
    unsigned char destinationmac3;
    unsigned char destinationmac4;
    unsigned char destinationmac5;
    unsigned char sourcemac0;
    unsigned char sourcemac1;
    unsigned char sourcemac2;
    unsigned char sourcemac3;
    unsigned char sourcemac4;
    unsigned char sourcemac5;
    L3PacketUDP L3;
    uint32_t crc;
    L2FrameUDP(unsigned char _destinationmac[6], unsigned char _sourcemac[6], L3PacketUDP l3, uint32_t _crc = 0) : sourcemac0{_sourcemac[0]}, sourcemac1{_sourcemac[1]}, sourcemac2{_sourcemac[2]}, sourcemac3{_sourcemac[3]}, sourcemac4{_sourcemac[4]}, sourcemac5{_sourcemac[5]}, destinationmac0{_destinationmac[0]}, destinationmac1{_destinationmac[1]}, destinationmac2{_destinationmac[2]}, destinationmac3{_destinationmac[3]}, destinationmac4{_destinationmac[4]}, destinationmac5{_destinationmac[5]}, L3{l3}, crc{_crc} {
      //memcpy((void *)&destinationmac, (const void *)&_destinationmac, sizeof(destinationmac));
      //memcpy((void *)&sourcemac, (const void *)&_sourcemac, sizeof(sourcemac));
    }
    L2FrameUDP& operator=(const L2FrameUDP&) = default;
};
class RouteEntry {
public:
    uint32_t ip;
    uint32_t mask;
    uint8_t macaddress[6];
    RouteEntry(uint32_t _ip, uint32_t _mask, uint8_t _macaddress[6]) : ip{_ip}, mask{_mask} {
      memcpy((void*) macaddress, (const void *) _macaddress, sizeof(macaddress));
    }
    RouteEntry& operator=(const RouteEntry&) = default;
};

// these are meant to transport data from the io thread to the routing one
// pair arguments: unsigned char - eth channel id (0-3), L2FrameUDP - the frame
std::queue<std::pair<unsigned char, L2FrameUDP>> outgoingdata;
std::queue<std::pair<unsigned char, L2FrameUDP>> incomingdata;

volatile bool is_writing = false;

unsigned int gpiomapping[24] = {27, 28, 3, 5, 7, 29, 31, 26, 24, 21, 19, 23, 32, 33, 8, 10, 36, 11, 12, 35, 38, 40, 15, 16};

std::thread router;

volatile int eth[4] = { serialOpen("/dev/ttyAMA1", 9600), serialOpen("/dev/ttyAMA2", 9600), serialOpen("/dev/ttyAMA3", 9600), serialOpen("/dev/ttyAMA4", 9600)};

uint8_t nulladdress[6] = {0, 0, 0, 0, 0, 0};
RouteEntry routingtable[4] = {RouteEntry(0, 0, nulladdress), RouteEntry(0, 0, nulladdress), RouteEntry(0, 0, nulladdress), RouteEntry(0, 0, nulladdress)};

//volatile int firstdiscards[4] = {0, 0, 0, 0};

volatile int serverip[4] = {0x0A000001, 0x0A010001, 0x0A020001, 0x0A030001};

uint8_t addressincrement[4] = {1, 1, 1, 1};

void printpacket(L2FrameUDP frame) {
    while(is_writing == true) {}
    is_writing = true;
    //std::cout << "L2 Sourcemac: " << std::bitset<8>(frame.sourcemac0) << std::bitset<8>(frame.sourcemac1) << std::bitset<8>(frame.sourcemac2) << std::bitset<8>(frame.sourcemac3) << std::bitset<8>(frame.sourcemac4) << std::bitset<8>(frame.sourcemac5) << '\n';
    //std::cout << "L2 Destinationmac: " << std::bitset<8>(frame.destinationmac0) << std::bitset<8>(frame.destinationmac1) << std::bitset<8>(frame.destinationmac2) << std::bitset<8>(frame.destinationmac3) << std::bitset<8>(frame.destinationmac4) << std::bitset<8>(frame.destinationmac5) << '\n';
    std::cout << "L2 Sourcemac: " << std::hex << (uint32_t)frame.sourcemac0 << ':' << (uint32_t)frame.sourcemac1 << ':' << (uint32_t)frame.sourcemac2 << ':' << (uint32_t)frame.sourcemac3 << ':' << (uint32_t)frame.sourcemac4 << ':' << (uint32_t)frame.sourcemac5 << std::dec << '\n';
    std::cout << "L2 Destinationmac: " << std::hex << (uint32_t)frame.destinationmac0 << ':' << (uint32_t)frame.destinationmac1 << ':' << (uint32_t)frame.destinationmac2 << ':' << (uint32_t)frame.destinationmac3 << ':' << (uint32_t)frame.destinationmac4 << ':' << (uint32_t)frame.destinationmac5 << std::dec << '\n';
    std::cout << "L2 CRC: " << frame.crc << '\n';
    std::cout << "L3 Sourceip: " << (uint32_t)((frame.L3.sourceip >> (3*8)) & 0b11111111) << '.' << (uint32_t)((frame.L3.sourceip >> (2*8)) & 0b11111111) << '.' << (uint32_t)((frame.L3.sourceip >> (1*8)) & 0b11111111) << '.' << (uint32_t)(frame.L3.sourceip & 0b11111111) << '\n';
    std::cout << "L3 Destinationip: " << (uint32_t)((frame.L3.destinationip >> (3*8)) & 0b11111111) << '.' << (uint32_t)((frame.L3.destinationip >> (2*8)) & 0b11111111) << '.' << (uint32_t)((frame.L3.destinationip >> (1*8)) & 0b11111111) << '.' << (uint32_t)(frame.L3.destinationip & 0b11111111) << '\n';
    std::cout << "L3 Sourcemask: " << "0x" << std::hex << frame.L3.sourcemask << std::dec << '\n';
    std::cout << "L3 TTL: " << (uint32_t)frame.L3.TTL << '\n';
    std::cout << "L4 Istcp: " << frame.L3.L4.istcp << '\n';
    std::cout << "L4 Sourceport: " << (uint32_t)frame.L3.L4.sourceport << '\n';
    std::cout << "L4 Destinationport: " << (uint32_t)frame.L3.L4.destinationport << '\n';
    std::cout << "L5 Sessionid: " << (uint32_t)frame.L3.L4.L5.sessionid << '\n';
    std::cout << "L6 Protocolid: " << (uint32_t)frame.L3.L4.L5.L6.protocolid << '(' << ((((uint32_t)frame.L3.L4.L5.L6.protocolid >= 0) && ((uint32_t)frame.L3.L4.L5.L6.protocolid <= 2)) ? protocols[frame.L3.L4.L5.L6.protocolid] : "UNKNOWN") << ")\n";
    std::cout << "L7 Data: " << "{ " << (uint32_t)frame.L3.L4.L5.L6.L7.data0 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data1 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data2 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data3 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data4 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data5 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data6 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data7 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data8 << ", ";
    std::cout << (uint32_t)frame.L3.L4.L5.L6.L7.data9  << " }" << std::endl;
    is_writing = false;
}

void routingthread() {
    while(true) {
        while(!incomingdata.empty()) {
            //std::bitset<sizeof(L2FrameUDP) * 8> bits = incomingdata.front().second;
            //for(int i = 0; i < 352/2; i++) {
            //   bool t = bits[i];
            //   bits[i] = bits[352-1-i];
            //   bits[352-1-i] = t;
            //}
            L2FrameUDP frame = incomingdata.front().second;
            uint8_t ethid = incomingdata.front().first;
            //std::cout << "First eight bytes" << (unsigned int)bits[351-0] << (unsigned int)bits[351-1] << (unsigned int)bits[351-2] << (unsigned int)bits[351-3] << (unsigned int)bits[351-4] << (unsigned int)bits[351-5] << (unsigned int)bits[351-6] << (unsigned int)bits[351-7] << '\n';
            incomingdata.pop();
            std::cout << "\nReceived packet: " << std::endl;;
            printpacket(frame);
            // handle dhcp
            if((frame.destinationmac0 == 0xFF) && (frame.destinationmac1 == 0xFF) && (frame.destinationmac2 == 0xFF) && (frame.destinationmac3 == 0xFF) && (frame.destinationmac4 == 0xFF) && (frame.destinationmac5 == 0xFF) && (frame.L3.L4.destinationport == 67) && (frame.L3.L4.sourceport == 68) && (frame.L3.L4.L5.L6.protocolid == 0) && (frame.L3.L4.L5.L6.L7.data0 == 53)) {
                if(frame.L3.L4.L5.L6.L7.data1 == 0x1) { // DHCPDISCOVER(1) -> DHCPOFFER(2)
                    if(routingtable[ethid].ip == 0) {
                        addressincrement[ethid]++;
                        if(addressincrement[ethid] == 0 || addressincrement[ethid] == 1 || addressincrement[ethid] == 255) addressincrement[ethid] = 2;
                        uint8_t offerdata[10] = {53, 0x2, 10, ethid, 0x0, addressincrement[ethid], 0xFF, 0xFF, 0xFF, 0x0 };
                        L7Data l7(offerdata);
                        L6Data l6(0, l7);
                        L5Session l5(0, l6);
                        L4DatagramUDP l4(68, 67, l5, false);
                        L3PacketUDP l3(0x0A000000 + (ethid << (8 * 2)) + addressincrement[ethid], serverip[ethid], 0xFFFFFF00, 254, l4);
                        uint8_t destmac[6] = {frame.sourcemac0, frame.sourcemac1, frame.sourcemac2, frame.sourcemac3, frame.sourcemac4, frame.sourcemac5};
                        L2FrameUDP l2(destmac, macaddress, l3, 0x0);
                        std::cout << "\nSent packet: " << std::endl;
                        printpacket(l2);
                        outgoingdata.emplace(std::pair<uint8_t, L2FrameUDP>(ethid, l2));
                    }
                } else if (frame.L3.L4.L5.L6.L7.data1 == 0x3) {  // DHCPREQUEST(3) -> DHCPACKNOWLEDGE(5)
                    if(routingtable[ethid].ip == 0) {
                        uint8_t offerdata[10] = {53, 0x5, 10, ethid, 0x0, frame.L3.L4.L5.L6.L7.data5, 0xFF, 0xFF, 0xFF, 0x0 };
                        L7Data l7(offerdata);
                        L6Data l6(0, l7);
                        L5Session l5(0, l6);
                        L4DatagramUDP l4(68, 67, l5, false);
                        L3PacketUDP l3(0x0A000000 + (ethid << (8 * 2)) + (frame.L3.destinationip & 0b11111111),serverip[ethid], 0xFFFFFF00, 254, l4);
                        uint8_t destmac[6] = {frame.sourcemac0, frame.sourcemac1, frame.sourcemac2, frame.sourcemac3, frame.sourcemac4, frame.sourcemac5};
                        L2FrameUDP l2(destmac, macaddress, l3, 0x0);
                        std::cout << "\nSent packet: " << std::endl;
                        printpacket(l2);
                        RouteEntry entry(0x0A000000 + (ethid << (8 * 2)) + (frame.L3.destinationip & 0b11111111), 0xFFFFFF00, destmac);
                        routingtable[ethid] = entry;
                        outgoingdata.emplace(std::pair<uint8_t, L2FrameUDP>(ethid, l2));
                    }
                }
            }
            // handle ARP
            else if(frame.L3.L4.L5.L6.protocolid == 4 && frame.L3.destinationip == serverip[ethid]) {
                uint32_t searchedaddress[6] = {frame.L3.L4.L5.L6.L7.data0, frame.L3.L4.L5.L6.L7.data1, frame.L3.L4.L5.L6.L7.data2, frame.L3.L4.L5.L6.L7.data3, frame.L3.L4.L5.L6.L7.data4, frame.L3.L4.L5.L6.L7.data5};
                uint32_t result = -1;
                for(int i = 0; i < 4; i++) {
                    for(int i2 = 0; i2 < 6; i2++){
                        if(routingtable[i].macaddress[i2] != searchedaddress[i2]) goto afterloop;
                    }
                    result = routingtable[i].ip;
                afterloop:
                    searchedaddress[0] = searchedaddress[0];
                }
                if(result != -1) {
                    uint8_t arpdata[10] = {(result >> (3 * 8)) & 0b11111111, (result >> (2 * 8)) & 0b11111111, (result >> (1 * 8)) & 0b11111111, result & 0b11111111, 0, 0, 0, 0, 0, 0};
                    L7Data l7(arpdata);
                    L6Data l6(0, l7);
                    L5Session l5(0, l6);
                    L4DatagramUDP l4(0, 0, l5, false);
                    L3PacketUDP l3(frame.L3.sourceip, serverip[ethid], 0xFFFFFF00, 254, l4);
                    uint8_t destmac[6] = {frame.sourcemac0, frame.sourcemac1, frame.sourcemac2, frame.sourcemac3, frame.sourcemac4, frame.sourcemac5};
                    L2FrameUDP l2(destmac, macaddress, l3, 0x0);
                    std::cout << "\nSent packet: " << std::endl;
                    printpacket(l2);
                    outgoingdata.emplace(std::pair<uint8_t, L2FrameUDP>(ethid, l2));
                }
            }
            // handle icmp and send
            else if(((frame.L3.L4.L5.L6.protocolid == 1) || (frame.L3.L4.L5.L6.protocolid == 2)) )  {
                frame.sourcemac0 = macaddress[0];
                frame.sourcemac1 = macaddress[1];
                frame.sourcemac2 = macaddress[2];
                frame.sourcemac3 = macaddress[3];
                frame.sourcemac4 = macaddress[4];
                frame.sourcemac5 = macaddress[5];
                frame.L3.TTL -= 1;
                int destid = -1;
                for(int i = 0; i < 4; i++) {
                    if(routingtable[i].ip == frame.L3.destinationip) {
                        destid = i;
                    }
                }
                if(destid != -1) {
                    std::cout << "\nSent packet: " << std::endl;
                    printpacket(frame);
                    outgoingdata.emplace(std::pair<uint8_t, L2FrameUDP>(destid, frame));
                }
            }

        }
    }
}
void pollForPackets() {
    int lastavail[4] = {0, 0, 0, 0};
    int samecounter[4] = {0, 0, 0, 0};

    //bool firstpacket[4] = {true, true, true, true};

    while(true) {
        if(!outgoingdata.empty()) {
            uint8_t ethid = outgoingdata.front().first;
            L2FrameUDP frame = outgoingdata.front().second;
            outgoingdata.pop();
            uint8_t* bytes = reinterpret_cast<uint8_t*>(&frame);
            for(int i = 0; i < sizeof(L2FrameUDP); i++) {
                serialPutchar(eth[ethid], bytes[i]);
            }
        }
        while(is_writing == true) {}
        is_writing = true;
        int avail[4] = {serialDataAvail(eth[0]), serialDataAvail(eth[1]), serialDataAvail(eth[2]), serialDataAvail(eth[3])};
        std::cout << "Bytes available: " << avail[0] << '/' << avail[1] << '/' << avail[2] << '/' << avail[3] << std::endl;
        is_writing = false;
        for(int i = 0; i < 4; i++) {
            if(avail[i] == lastavail[i]) {
                samecounter[i]++;
                if(samecounter[i] >= 180) { // 180s, after 3 minutes, the port is automaticall freed
                    serialFlush(i);
                    //firstpacket[i] = true;
                    routingtable[i] = RouteEntry(0, 0, nulladdress);
                    samecounter[i] = 0;
                    while(is_writing == true) {}
                    is_writing = true;
                    std::cout << "Connection lost, flushing eth" << i << std::endl;
                    is_writing = false;
                }
            } else {
                lastavail[i] = avail[i];
                samecounter[i] = 0;
            }
            if(avail[i] >= sizeof(L2FrameUDP)) {
                int multiple = 0;
                while((multiple * sizeof(L2FrameUDP)) < avail[i]) {
                    multiple = multiple + 1;
                }
                multiple = multiple - 1;
                for(int i2 = 0; i2 < (avail[i] - (multiple * sizeof(L2FrameUDP))); i2++) {
                    serialGetchar(eth[i]);
                }
                while(avail[i] >= sizeof(L2FrameUDP)) {
                    uint8_t bytes[sizeof(L2FrameUDP)];
                    for(int i2 = 0; i2 < sizeof(L2FrameUDP); i2++) {
                        bytes[i2] = serialGetchar(eth[i]);
                    }
                    std::bitset<sizeof(L2FrameUDP) * 8> bitset = *(reinterpret_cast<std::bitset<sizeof(L2FrameUDP) * 8>*>(bytes));
                    L2FrameUDP received = *(reinterpret_cast<L2FrameUDP*>(&bitset));
                    //if(firstdiscards[i] == 4) {
                    incomingdata.emplace(std::pair<unsigned char, L2FrameUDP>(i, received));
                    //} else {
                    //    firstdiscards[i]++;
                    //}
                    avail[i] -= sizeof(L2FrameUDP);
                }
            }
        }
        delay(1000);
    }

    /*
    setup(); //setup pi-gpio
    for(int i = 0; i < 23; i+=6) {
        // init pins 0-23
        // eth0: pins 0-5
        // input: pins 0-2, output: pins 3-5
        
        // eth1: pins 6-11
        // input: pins, 6-8, output: 9-11
        
        // eth2: pins 12-17
        // input: pins 12-14, output: 15-17

        // eth3: pins 18-23
        // input: pins 18-20, output: 21-23
        setup_gpio(i,   OUTPUT, 0);
        setup_gpio(i+1, OUTPUT, 0);
        setup_gpio(i+2, OUTPUT, 0);
        output_gpio(i, LOW);
        output_gpio(i+1, LOW);
        output_gpio(i+2, LOW);
        setup_gpio(i+3, INPUT, 0);
        setup_gpio(i+4, INPUT, 0);
        setup_gpio(i+5, INPUT, 0);
    }
    
    while(true) {
        std::bitset<sizeof(L2FrameUDP) * 8> packet(0x0);
        //std::cout << "Starting to receive packet" << std::endl;
        while(true) {
            if((input_gpio(3) == 1) && (input_gpio(4) == 1) && (input_gpio(5) == 1)) {
                goto waitforinput;
            }
        }
        
    waitforinput:
        //std::cout << "Preamble received\n";
        while(true) {
            int bit1 = input_gpio(3);
            int bit2 = input_gpio(4);
            int bit3 = input_gpio(5);
            if((bit1 != 1) || (bit2 != 1) || (bit2 != 1)) {
                packet.set(0, bit1);
                packet.set(1, bit2);
                packet.set(2, bit3);
                //std::cout << "First bits received\n";
                goto receivepacket;
            }
            delayMicroseconds(40);
        }
    receivepacket:
        //int lastbit1 = 0, lastbit2 = 0, lastbit3 = 0, repeatcounter = 0;
        for(int i = 3; i < ((sizeof(L2FrameUDP) * 8) - 4); i+=3) {
            int bit1 = input_gpio(3);
            int bit2 = input_gpio(4);
            int bit3 = input_gpio(5);
            /*if((bit1 == lastbit1) && (bit2 == lastbit2) && (bit3 == lastbit3)) {
                repeatcounter++;
            } else {
                repeatcounter = 0;
            }
            lastbit1 = bit1;
            lastbit2 = bit2;
            lastbit3 = bit2;
            */ /*
            packet.set(i, (bool)(bit1 & 0x1));
            packet.set(i+1, (bool)(bit2 & 0x1));
            packet.set(i+2, (bool)(bit3 & 0x1));
            delayMicroseconds(40);
        }
        packet.set(351, (bool)(input_gpio(3) & 0x1));
        //if(repeatcounter > 50) {
        //    std::cout << "Packet probably dropped" << std::endl;
        //}
        
        //std::cout << "Packet received" << std::endl;
        incomingdata.emplace(std::pair<unsigned char, std::bitset<sizeof(L2FrameUDP) * 8>>(0, packet));
        //unsigned char bytes[sizeof(L2FrameUDP)];
        //for(int i = 0; i < (sizeof(L2FrameUDP) * 8) - 1; i+=8) {
        //    bytes[i] = (unsigned char)((packet[i] << 7) + (packet[i] << 6) + (packet[i] << 5) + (packet[i] << 4) + (packet[i] << 3) + (packet[i] << 2) + (packet[i] << 1) + packet[i]);
        //}
        //L2FrameUDP receivedpacket = reinterpret_cast<L2FrameUDP&>(bytes);
    }
    */
    
}

int main() {
    
    serialFlush(eth[0]);
    serialFlush(eth[1]);
    serialFlush(eth[2]);
    serialFlush(eth[3]);

    router = std::thread(routingthread);

    if(wiringPiSetupSys() == -1) {
        return 1;
    }

    pollForPackets();
    
    std::cout << "Stopping" << std::endl;
    std::string halt;
    std::cin >> halt;

    for(int i = 0; i < 4; i++) {
        serialClose(eth[i]);
    }
}

