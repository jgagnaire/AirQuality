
#include <SD.h>
#include <Ethernet.h>
#include <TinyGPS++.h>
#include <EthernetClient.h>
#include <SoftwareSerial.h>
#include <MutichannelGasSensor.h>

static SoftwareSerial serial_connection(2, 3);
static TinyGPSPlus gps;
static char mac_address[] = {0xCA, 0xFE, 0xBA, 0xBE, 0xBE, 0xEF};
static IPAddress client_ip[] = {192, 168, 1, 210}; // if DHCP fails to assign us an IP
static IPAddress server_ip[] = {192, 168, 1, 69}; // no DNS
//static char server_name[] = "localhost"; // using DNS
static EthernetClient client;
static bool is_online = false;
static bool data_to_send = false;
static File datafile;
static unsigned long previous_time = 0;
static unsigned long time_interval = 300000; // <=> 5 minutes

#ifdef SERVER_PORT
# undef SERVER_PORT
#endif // !SERVER_PORT
#define SERVER_PORT 8000

#ifdef DATAFILE_NAME
# undef DATAFILE_NAME
#endif // !DATAFILE_NAME
#define DATAFILE_NAME "log.txt"


static void setup_no2_sensor()
{
  gas.begin(0x04);
  gas.powerOn();
  Serial.println("sensor OK");
}

static void setup_gps_module()
{
  serial_connection.begin(9600);
  Serial.println("GPS init OK!");
}

static void setup_ethernet_module()
{
  if (!Ethernet.begin(mac_address))
  {
    Serial.println("Ethernet configuration with DCHP failed, trying without...");
    Ethernet.begin(mac_address, client_ip);
  }
  delay(1000); // leave 1 second for the Ethernet controller to be online
  Serial.println("Ethernet online OK");
  if (client.connect(server_ip, SERVER_PORT) == 1)
    Serial.println("Connected to the server!");
}

static void setup_sd_module()
{
  if (SD.begin(4))
    Serial.println("SD init OK!");
}

void setup()
{
  Serial.begin(9600);

  setup_no2_sensor();
  setup_gps_module();
  setup_ethernet_module();
  setup_sd_module();
}

static String create_query_string()
{
  String query;
  
  query.concat("GET /handle_request?longitude=");
  query.concat(gps.location.lng());
  query.concat("&latitude=");
  query.concat(gps.location.lat());
  query.concat("&value=");
  query.concat(gas.measure_NO2());
  query.concat(" HTTP/1.1");
  return query;
}

static String create_host_string()
{
  String host;

  host.concat("Host: ");
  host.concat(server_ip[0]);
  host.concat(server_ip[1]);
  host.concat(server_ip[2]);
  host.concat(server_ip[3]);
  host.concat(":");
  host.concat(SERVER_PORT);
  return host;
}

static void send_data()
{
  client.println(create_query_string());
  client.println(create_host_string());
  client.println("Connection: close");
  client.println();
}

static bool open_datafile()
{
  if (!datafile)
  {
     datafile = SD.open(DATAFILE_NAME, FILE_WRITE);
     if (!datafile)
     {
        Serial.println("Error while opening datafile on MicroSD card");
        return false;
     }
  }
  return true;
}

static void write_to_sd()
{
  if (!open_datafile())
    return ;

  datafile.println(create_query_string());
  data_to_send = true;
}

static bool send_sd_data()
{
  if (!open_datafile())
    return false;
  datafile.seek(0);
  
  while (datafile.position() < datafile.size())
  {
    String buf;
    char ret = datafile.read();

    if (ret == '\n' && buf.length() > 0)
    {
      client.println(buf);
      client.println(create_host_string());
      client.println("Connection: close");
      client.println();
      buf.remove(0);
    }
    else
      buf.concat(ret);
  }
  
  if (!SD.remove(DATAFILE_NAME) || !SD.open(DATAFILE_NAME, FILE_WRITE))
    {
      Serial.println("Error while clearing the datafile");
      datafile.seek(0);
    }

  return true;
}

void loop()
{
  unsigned long current_time = millis();
  if (current_time - previous_time < time_interval)
    return ;
  previous_time = current_time;
  
  if (!client.connected() && data_to_send
      && client.connect(server_ip, SERVER_PORT) && send_sd_data())
        data_to_send = false;

  while (serial_connection.available())
    gps.encode(serial_connection.read());
  
  if (gps.location.isValid())
  {
    if (client.connected())
      send_data();
    else
      write_to_sd();
  }
}
