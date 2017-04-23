#include <MemoryFree.h>

#include <Base64.h>
//Pin to wich the light sensor is attached.
#define PIN_LIGHT A0

//Definitions for packet slot identifiers.
#define PACKET_SLOT_IN 0
#define PACKET_SLOT_OUT 1

//Output Packet Types.
#define PACKET_TYPE_STRING 0
#define PACKET_TYPE_INT 1

//Input Packet Types.
#define QUERY_TYPE_LIGHT 0

//Very Simple Packet Structure.
typedef struct {
  //The type of the data for the packet
  uint8_t payload_type;
  //The size of the contained data.
  uint8_t payload_size;
  //The data.
  uint8_t* payload_data;
  //Whether or not this packet slot is busy. Should probably be made private or at least have some number of underscores added to it. Should not be chaned by client code.
  bool is_busy;
} packet_t;

//Declare the two packet slots we're going to use. 
packet_t input_packet, output_packet;

//Start using a packet slot. Initializes the specified packet with a payload of a given size. 
bool packet_begin(uint8_t payload_size, uint8_t packet_slot);

//Start using a packet slot. Initialize the specified packet with data from the given base 64 encoded string.
bool packet_begin(char* encoded_message, uint8_t len, uint8_t packet_slot);

//Stop using a packet slot. Destroys all data stored in the packet. 
bool packet_end(uint8_t packet_direction);

//Sends the output packet through the serial port. 
void io_send_output_packet();

//Send an integer over the serial port.
void io_send_int(int value);

//Send a string over the serial port.
void io_send_message(String message);


//Determines whether or not there's input waiting to be processed.
bool input_waiting = false;

//A buffer to collect serial port data.
String input_buffer = "";


//handle an individual input packet.
void io_handle_input_packet(String packet);

//Process all input.
void io_input_dispatch();

//Collect input from serial port.
void io_handle_input_event();


//Make sure the packet_begin/end functions don't leak memory.
void test_packet_out_lifetime(){
  int sum=0;
  for(int i=0;i<10;i++){
    int before=freeMemory();
    packet_begin(4,PACKET_SLOT_OUT);
    packet_end(PACKET_SLOT_OUT);
    int after=freeMemory();
    Serial.print("packet_begin/packet_end"+String(before-after)+"\n");
  }
}

//Make sure the packet_begin/end functions don't leak memory.
void test_packet_in_lifetime(){
  int sum=0;
  for(int i=0;i<10;i++){
    int before=freeMemory();
    packet_begin("AAA=",4,PACKET_SLOT_IN);
    packet_end(PACKET_SLOT_IN);
    int after=freeMemory();
    Serial.print("packet_begin/packet_end:"+String(before-after)+"\n");
  }  
}

//Iniitialize the serial port.
void setup() {
  Serial.begin(115200);
 
  // put your setup code here, to run once:

}

void loop() {
  // test_packet_out_lifetime();
  // test_packet_in_lifetime();
  // delay(1000);
  
  //io_send_message("STARTING LOOP");
  //io_send_message(String(freeMemory()));
  //io_send_message(input_buffer);
  //io_send_message("Waiting On Input "+input_waiting?"YES":"NO");
  //Process all the input!
  io_input_dispatch();

  //The sensor currently hooked up to this has a response time of 20-30ms, so updating faster than this is pointless.
  delay(30);
  
  //io_send_message("ENDING LOOP");
  // put your main code here, to run repeatedly:
}

void io_handle_input_packet(String encoded_packet) {
  //io_send_message("Handling " + encoded_packet);

  //Try to fill the input slot with the data. If we can't, fail. 
  if (!packet_begin((char*)encoded_packet.c_str(), (uint8_t)encoded_packet.length(), PACKET_SLOT_IN)) return;
  switch (input_packet.payload_type) {
    //We should probably define some better query contstants for this. 
    case 0:
  //    io_send_message("Handling Sensor Packet " + encoded_packet);
      io_send_int(analogRead(PIN_LIGHT));
      break;
    default:
      //If we don't know what to do with a given string, tell the client about it.
   //   io_send_message("Handling Unknown Packet" + encoded_packet);
      io_send_message("BAD QUERY");
      break;

  }
  //Tell the system we're done using the input packet.
  packet_end(PACKET_SLOT_IN);
}

void io_input_dispatch() {
  //int before=freeMemory();
  //Initialize some space for holding individual packets.
  String packet_buffer = "";
  //If there's data to be processed...
  if (input_waiting) {
    //Tell the system we've handled it.
    input_waiting = false;

    //Loop through all ******\n segments, processing them and removing them from the input buffer, leaving whatever's left over in the buffer for the next loop.
    for (size_t idx = 0; idx < input_buffer.length(); idx++) {
      if (input_buffer[idx] == '\n'){
        //Get the next packet string
      packet_buffer = input_buffer.substring(0, idx);
      //Process it.
      io_handle_input_packet(packet_buffer);
      //Remove it from the buffer.
      input_buffer = input_buffer.substring(idx + 1);
      //Repeat.
      idx = 0;
      }
    }
  }
  //int after=freeMemory();
  //io_send_message("BYTES USED BY io_input_dispatch: "+String(after-before));
}
void io_handle_input_event() {

  //While we have serial data avialable.
  while (Serial.available()) {

    //remove a character from the port.
    //We should be able to make this faster by reading the entire buffer at once, but the doc's don't show this as being an option. 
    char c = Serial.read();

    //If there's a new line character.
    if (c == '\n') {
      //If adding the newline character to the buffer wouldn't just create an empty command.
      if (input_buffer.length() > 0) {
        //Tell the loop() function there's input for it to process.
        input_waiting = true;
        //add the character to the buffer.
        input_buffer += c;
      }
    } else {
      //Otherwise, add the character to the buffer.
      input_buffer += c;
    }
  }
}
void serialEvent() {

  //When we get data on the serial port, buffer it.
  io_handle_input_event();
}


void io_send_output_packet() {
  //If the output packet isn't being used, there's nothing to send.
  if (!output_packet.is_busy) return;
  
  //Store the size of the full packet.
  size_t data_size = 2 + output_packet.payload_size;

  //Get a stack buffer to hold the data we're going to send.
  char plain_data[data_size];

  //Create a write pointer to keep track of where we're writing to.
  size_t wp = 0;

  //write the payload type to the data.
  plain_data[wp] = output_packet.payload_type;
  wp++;

  //write the payload size to the data.
  plain_data[wp] = output_packet.payload_size;
  wp++;

  //write the payload to the data.
  for (size_t idx = 0; idx < output_packet.payload_size; idx++) {
    plain_data[wp + idx] = output_packet.payload_data[idx];
  }
  wp += output_packet.payload_size;

  //encode the packet for transmission.
  size_t output_size = Base64.encodedLength(data_size);
  char encoded_data[output_size];
  Base64.encode(encoded_data, plain_data, data_size);
  String encoded_string = String(encoded_data);

  //Send the packet. 
  Serial.print(encoded_string + "\n");
}
void io_send_int(int value) {
  
  if (packet_begin(sizeof(int), PACKET_SLOT_OUT)) {
    output_packet.payload_type = PACKET_TYPE_INT;
    union {
      int value;
      uint8_t bytes[sizeof(int)];
    } converter;
    converter.value = value;
    for (int idx = 0; idx < sizeof(int); idx++) {
      output_packet.payload_data[idx] = converter.bytes[idx];
    }
    io_send_output_packet();
    packet_end(PACKET_SLOT_OUT);
  }
}
void io_send_message(String message) {
  if (packet_begin(message.length() + 1, PACKET_SLOT_OUT)) {
    output_packet.payload_type = PACKET_TYPE_STRING;
    //  Serial.println(message.length());
    const char* message_data = message.c_str();
    //  Serial.print("Prepare: Message Before Copy To Payload: ");
    //  Serial.println(message_data);
    message.getBytes(output_packet.payload_data, output_packet.payload_size);
    //  Serial.print("Prepare: Message In Payload: ");
    //  Serial.println((char*)output_packet.payload_data);
    io_send_output_packet();
    packet_end(PACKET_SLOT_OUT);
  }
}
inline bool __packet_slot_decode(uint8_t packet_slot, packet_t** packet) {
  if (packet_slot == PACKET_SLOT_IN) {
    *packet = &input_packet;
  } else if (packet_slot == PACKET_SLOT_OUT) {
    *packet = &output_packet;
  } else {
    return false;
  }
  return true;
}
bool packet_begin(uint8_t payload_size, uint8_t packet_slot) {


  //Set up the targeted packet.
  packet_t* packet;
  if (!__packet_slot_decode(packet_slot, &packet)) return false;

  //Validate the arguments.
  if (packet->is_busy) return false;
  if (payload_size > 68) return false;

  //Iniitalize the packet.
  packet->payload_type = -1;
  packet->payload_size = payload_size;
  packet->payload_data = new uint8_t[payload_size];
  for (size_t idx = 0; idx < payload_size; idx++) {
    packet->payload_data[idx] = 0;
  }
  packet->is_busy = true;

  return true;
}
bool packet_begin(char* encoded_message, uint8_t len, uint8_t packet_slot) {
  
  //Set up the targeted packet.
  packet_t* packet;
  if (!__packet_slot_decode(packet_slot, &packet)) return false;

  //Validate the arguements.
  if (packet->is_busy) return false;
  if (len > 100) return false;

  //Decode the message.
  int32_t decoded_message_len = Base64.decodedLength(encoded_message, len);
  uint8_t decoded_message[decoded_message_len];
  
  Base64.decode((char*)decoded_message, (char*)encoded_message, len);
  //Setup the packet header.
  packet->payload_type = decoded_message[0];
  packet->payload_size = decoded_message[1];

  //Validate the packet size.
  if (packet->payload_size > 68) return false;

  //Allocate the payload for the packet.
  packet->payload_data = new uint8_t[packet->payload_size];
  
  //Copy payload into packet.
  //TODO: Needs Bounds Checking.
  for (uint8_t idx = 0; idx < packet->payload_size; idx++) {
    packet->payload_data[idx] = decoded_message[2 + idx];
  }
  packet->is_busy=true;
  return true;
}
bool packet_end(uint8_t packet_slot) {

  //Set up the targeted packet.
  packet_t* packet;
  if (!__packet_slot_decode(packet_slot, &packet)) return false;
  if (!packet->is_busy) return false;
  
  //Clean up the targeted packet.
  delete[] packet->payload_data;
  packet->payload_size = -1;
  packet->payload_type = -1;
  packet->payload_data = (uint8_t*) - 1;
  packet->is_busy = false;
}

