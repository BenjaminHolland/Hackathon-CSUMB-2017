#include <Base64.h>
#define PIN_LIGHT A0
#define PACKET_MAX_SIZE 70
#define PACKET_HEADER_SIZE 3
#define PACKET_MAX_PAYLOAD (PACKET_MAX_SIZE-(PACKET_HEADER_SIZE))
#define PACKET_MAX_ENCODED_SIZE 100

#define RESPONSE_STRING 0
#define RESPONSE_INT 1

#define QUERY_LIGHT 0

//Packet slot structure.
typedef struct{
  union{
    struct{
      struct{
      byte type;
      byte size;
      byte data[PACKET_MAX_PAYLOAD];
      } payload;
      byte xsum;
    } as_structure;
    byte as_bytes[PACKET_MAX_SIZE];
  } packet;
  bool is_busy;
} packet_slot;

/**
 * There are only two packet slots: input and output.
 * Each slot should be initialized before use using the io_tx/rx_init functions, and freed using io_tx/rx free functions after use.
 * Neither of these functions allocate memory or do anything more intensive than decoding base64 strings.
 * 
 * Each slot contains the data for a packet that has either just come in off the input bus (input_packet) or is being prepared for transmission (output_packet).
 */
packet_slot input_packet,output_packet;

/**
 * packet_begin(packet_slot&,byte)
 * Initializes a packet slot with a given payload size.
 */
bool packet_begin(packet_slot& slot,byte payload_size);

/**
 * packet_begin(packet_slot&,const char*,byte)
 * Initializes a packet slot by decoding the given encoded packet.
 */
bool packet_begin(packet_slot& slot,const char* encoded_packet,byte encoded_length);
/**
 * Mark the slot as ready for reuse.
 */
bool packet_end(packet_slot& slot);

/**
 * Compute the checksum for the payload.
 */
byte packet_payload_xsum(packet_slot& slot);

/**
 * io_tx_init(byte)
 * Initialize the output packet slot with the given payload size.
 */
#define io_tx_init(payload_size) packet_begin(output_packet,(payload_size))
/**
 * io_tx_free(void)
 * Free the output packet slot for reuse.
 */
#define io_tx_free() packet_end(output_packet)

/**
 * Prepare the packet for transmission and transmit it across the seiral port.
 */
bool io_tx_packet();

/**
 * Send a string packet across the serial port.
 */
bool io_tx_string(char* message,byte count);
/**
 * Send a 16 bit value across the serial port.
 */
bool io_tx_int(int value);

/**
 * Initialize the input packet by decoding an encoded packet.
 */
#define io_rx_init(encoded_packet,encoded_length) packet_begin(input_packet,(encoded_packet),(encoded_length))
/**
 * Prepare the input packet for reuse.
 */
#define io_rx_free() packet_end(input_packet)
/**
 * Read and handle the next encoded packet from the input buffer.
 */
bool io_rx_packet();
/**
 * Take action depending on the given packet.
 */
bool io_handle_packet(String packet);
/**
 * Handle all waiting packets.
 */
void io_dispatch();
/**
 * Collect new packets from the serial port.
 */
bool io_collect();
/**
 * Indicates whether there are packets to be processed. Read Only.
 */
bool io_input_waiting;
/**
 * The current input buffer. Read only.
 */
String io_input_buffer;

/**
 * Testing functions.
 */
void test_io_tx_string();
void test_io_tx_int();


void setup() {
  //Initialize the serial port.
  Serial.begin(115200);
}

void loop() {
  //Handle input.
  io_dispatch();  
  // put your main code here, to run repeatedly:
}

void serialEvent(){
  //Collect input.
  io_collect();
}

bool packet_begin(packet_slot& slot,byte payload_size){
  if(slot.is_busy) return false;
  if(payload_size>PACKET_MAX_PAYLOAD) return false;
  slot.is_busy=true;
  slot.packet.data.payload_size=payload_size;
  slot.packet.data.payload_type=-1;
  slot.packet.data.payload_xsum=-1;
  return true;  
}

bool packet_begin(packet_slot& slot,char* encoded_packet,byte encoded_length){
  if(slot.is_busy) return false;
  if(encoded_length>PACKET_MAX_ENCODED_SIZE) return false;
  Base64.decode((char*)slot.packet.as_bytes,encoded_packet,encoded_length);
  return packet_payload_xsum(slot)==slot.packet.data.payload_xsum;
}

bool packet_end(packet_slot& slot){
  if(!slot.is_busy) return false;
  slot.is_busy=false;
  slot.packet.data.payload_size=-1;
  return true;
}

byte packet_payload_xsum(packet_slot& slot){
  return -1;
}

bool io_tx_packet(){
  if(!output_packet.is_busy) return false;
  output_packet.packet.data.payload_xsum=packet_payload_xsum(output_packet);
  byte packet_size=PACKET_HEADER_SIZE+output_packet.packet.data.payload_size;
  byte encoded_packet_size=Base64.encodedLength(packet_size)+1;
  char encoded_packet[encoded_packet_size];
  Base64.encode(encoded_packet,(char*)output_packet.packet.as_bytes,(int)packet_size);
  encoded_packet[encoded_packet_size-1]='\n';
  Serial.write(encoded_packet,encoded_packet_size);
  return true;
}

bool io_tx_string(char* message,byte count){  
  if(!io_tx_init(count))return false;
  output_packet.packet.data.payload_type=RESPONSE_STRING;
  for(int idx=0;idx<count;idx++){
    output_packet.packet.data.payload[idx]=message[idx];
  }
  if(!io_tx_packet()) return false;
  if(!io_tx_free()) return false;
}

bool io_tx_int(int value){
  if(!io_tx_init(2)) return false;
  output_packet.packet.data.payload_type=RESPONSE_INT;
  byte* as_bytes=(byte*)(&value);
  for(int idx=0;idx<sizeof(int);idx++){
    output_packet.packet.data.payload[idx]=as_bytes[idx];
  }
  if(!io_tx_packet()) return false;
  if(!io_tx_free()) return false;
}

void test_io_tx_string(){
  io_tx_string("HELLO WORLD",11);
}

void test_io_tx_int(){
  io_tx_int(1023);
}

bool io_handle_packet(String encoded_packet){
  if(input_packet.is_busy) return false;
  if(!io_rx_init((char*)encoded_packet.c_str(),encoded_packet.length())) return false;
  switch(input_packet.packet.data.payload_type){
    case QUERY_LIGHT:
      io_tx_int(analogRead(PIN_LIGHT));
      break;
    default:
      String msg="ERROR: BAD QUERY";
      io_tx_string((char*)msg.c_str(),msg.length());
      break;
  }
  if(!io_rx_free()) return false;
  return true;
}

bool io_rx_packet(){
  for(int idx=0;idx<io_input_buffer.length();idx++){
    if(io_input_buffer[idx]=='\n'){
      
      String encoded_packet=io_input_buffer.substring(0,idx);
     
      io_handle_packet(encoded_packet);
      if(idx<io_input_buffer.length()-1){
        io_input_buffer=io_input_buffer.substring(idx+1);
      }else{
        io_input_buffer="";
      }
      return true;
    }
  }
  return false;
}

void io_dispatch(){
  if(io_input_waiting){
    while(io_rx_packet());
    io_input_waiting=false;
  }
}

bool io_collect(){
  char c;
  while(Serial.available()){
    c=Serial.read();
    if(c=='\n'){
      if(io_input_buffer.length()>0){
        io_input_buffer+=c;
        io_input_waiting=true;
      }
    }else{
      io_input_buffer+=c;
    }
  }
  return io_input_waiting;
}

