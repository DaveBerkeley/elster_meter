
/*
 Arduino code to read data from an Elster A100C electricity meter.

 Copyright (C) 2012 Dave Berkeley solar@rotwang.co.uk

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 USA
*/

// set pin numbers:
const int ledPin = 13;
const int intPin = 2;

#define BIT_PERIOD 416 // us
#define BIT_MARGIN 10 // us

  /*
   *  Buffer
   */

#define BUFF_SIZE 128

struct Buffer
{
  int data[BUFF_SIZE];
  int in;
  int out;
};

static void buff_init(struct Buffer* b)
{
  b->in = b->out = 0;
}

static int buff_full(struct Buffer* b)
{
  int next = b->in + 1;
  if (next >= BUFF_SIZE)
    next = 0;
  return next == b->out;
}

static int buff_add(struct Buffer* b, int d)
{
  int next = b->in + 1;
  if (next >= BUFF_SIZE)
    next = 0;
  if (next == b->out)
    return -1; // overfow error

  b->data[b->in] = d;
  b->in = next;  
  return 0;  
}

static int buff_get(struct Buffer* b, int* d)
{
  if (b->in == b->out)
    return -1;
  int next = b->out + 1;
  if (next >= BUFF_SIZE)
    next = 0;
  *d = b->data[b->out];
  b->out = next;
  return 0;  
}

  /*
   *  Called on every InfraRed pulse, ie. every '1' bit.
   */

typedef unsigned long stamp;

static stamp last_us;

#define BITS(t) (((t) + (BIT_PERIOD/2)) / BIT_PERIOD)

static struct Buffer bits;

void on_change(void)
{
  //  push the number of bit periods since the last interrupt to a buffer.
  const stamp us = micros();
  const int diff = us - last_us;
  last_us = us;

  const int bit_periods = BITS(diff);
  buff_add(& bits, bit_periods);
}

  /*
   *  Decode bit stream
   */

static struct Buffer bytes;

static int bit_data;
static int bit_index;

int add_bit(int state)
{
    bit_data >>= 1;

    if (state)
        bit_data += 0x200;

    if (bit_data)
        bit_index += 1;

    if (bit_index < 10)
        return 0;

    if (bit_data & 1) // start bit
    {
        if (!state) // stop bit
        {
            const int d = (bit_data >> 1) & 0xFF;
            // high represents '0', so invert the bits
            buff_add(& bytes, (d ^ 0xFF));
            bit_data = bit_index = 0;
            return 1;
        }
    }

    //  bad frame
    bit_data = bit_index = 0;
    return 0;
}

static void on_timeout()
{
  // too long since last bits count
  
  if (bit_index)
    while (bit_data) // flush with trailing '0' bits
      add_bit(0);  
  bit_data = bit_index = 0;
}

static void on_bits(int bits)
{
  // called on each bits count.
  // the bit periods represent a '1' followed by N '0's.

  if (bits < 1)
  {
    on_timeout();
    return;
  }

  // the elapsed bit periods since last interrupt
  // represents a '1' followed by 0 or more '0's.  
  add_bit(1);
  for (; bits > 1; bits--)
    add_bit(0);
}

  /*
   *
   */
   
static int decode_bit_stream(void)
{
  // look for a pause in the bit stream
  const stamp us = micros();
  const int diff = us - last_us;
  if (BITS(diff) > 20)
    on_timeout();

  // read the bit stream  
  int bit_count;
  if (buff_get(& bits, & bit_count) < 0)
    return -1;

  on_bits(bit_count);

  // read the byte stream decoded above
  int byte_data;
  if (buff_get(& bytes, & byte_data) < 0)
    return -1;

  return byte_data;
}

  /*
   *
   */

unsigned long bcdtol(const unsigned char* data, int bytes)
{
    unsigned long result = 0;
    for (int i = 0;  i < bytes; i++)
    {
        const unsigned char digit = *data++;
        result *= 100;
        result += 10 * (digit >> 4);
        result += digit & 0xF;
    }
    return result;
}

typedef void (*on_reading)(unsigned long reading);

class ElsterA100C
{
    // see Appendix B of the product manual
    struct info {
        char product[12]; // eg. "Elster A100C..."
        char firmware[9];
        unsigned char mfg_serial[3];
        unsigned char config_serial[2];
        char utility_serial[16];
        unsigned char meter_definition[3];
        unsigned char rate_1_import_kWh[5]; // the reading we are interested in!
        unsigned char rate_1_reserved[5];
        unsigned char rate_1_reverse_kWh[5];
        unsigned char rate_2_import_kWh[5];
        unsigned char rate_2_reserved_kWh[5];
        unsigned char rate_2_reverse_kWh[5];
        unsigned char reserved_01[1];
        unsigned char status;
        unsigned char error;
        unsigned char anti_creep[3];
        unsigned char rate_1_time[3];
        unsigned char rate_2_time[3];
        unsigned char power_up[3];
        unsigned char power_fail[2];
        unsigned char watchdog;
        unsigned char reverse_warning;
        unsigned char reserved_02[10];
    };

    unsigned char data[sizeof(info)];
    unsigned int idx;
    int reading;
    unsigned char last_4[4];
    on_reading handler;
public:
    ElsterA100C(on_reading cb)
    : idx(0), reading(0), handler(cb)
    {
    }

    int good_cs(unsigned char cs, unsigned char check)
    {
      // note : should be 'cs == check', but I'm seeing
      // systematic 1-bit errors which I can't track down.      

      /*
      Serial.print("bcc=");
      Serial.print(check, HEX);
      Serial.print(" cs=");
      Serial.print(cs, HEX);
      Serial.print(" xor=");
      Serial.print(cs ^ check, HEX);
      Serial.print("\r\n");
      */

      int bits = 0;
      int delta = cs ^ check;
      for (; delta; delta >>= 1)
        if (delta & 0x01)
          bits += 1;

      return bits < 2;
    }

    void good_packet()
    {
        struct info* info = (struct info*) data;        
        handler(bcdtol(info->rate_1_import_kWh, 5));
    }

    unsigned char bcc(unsigned char cs, const unsigned char* data, int count)
    {
        for (int i = 0; i < count; ++i)
            cs += *data++;
        return cs;
    }

    void on_data(unsigned char c)
    {
        // match the packet header
        const unsigned char match[] = { 0x01, 0x00, sizeof(info), 0x02 };

        if (!reading)
        {
            // keep a log of the last 4 chars
            last_4[0] = last_4[1];
            last_4[1] = last_4[2];
            last_4[2] = last_4[3];
            last_4[3] = c;

            if (memcmp(match, last_4, sizeof(match)) == 0)
            {
                idx = 0;
                reading = 1;
                return;
            }

            return;
        }

        if (idx < sizeof(info))
        {
            data[idx++] = c;
            return;
        }

        const unsigned char etx = 0x03;
        if (idx == sizeof(info))
        {
            if (c != etx)
            {
                printf("etx=%02X\n", c);
                reading = idx = 0;
                return;
            }
            idx++;
            return;
        }                    

        unsigned char cs = 0x00; // why 0x40?
        cs = bcc(cs, match, sizeof(match));
        cs = bcc(cs, data, sizeof(data));
        cs = bcc(cs, & etx, 1);
            
        if (good_cs(cs, c))
          good_packet();

        reading = 0;
    }
};

  /*
   *
   */

void meter_reading(unsigned long reading)
{
  Serial.print(reading);
  Serial.print("\r\n");

  static int state = 0;
  digitalWrite(ledPin, state = !state);
}

ElsterA100C meter(meter_reading);

void setup() 
{
  buff_init(& bits);
  buff_init(& bytes);

  pinMode(ledPin, OUTPUT);
  pinMode(intPin, INPUT);

  attachInterrupt(0, on_change, RISING);

  Serial.begin(9600);
}

void loop()
{
  int byte_data = decode_bit_stream();
  if (byte_data == -1)
    return;

  meter.on_data(byte_data);
}

// FIN
