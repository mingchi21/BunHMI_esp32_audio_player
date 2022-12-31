//**********************************************************************************************************
//*    ESP32 Audio player with BunHMI,                                                              *
//      Audio Output: MAX98357A
//      For detail of BunHMI: https://shop.mcuidea.com
//**********************************************************************************************************
//

#include "Arduino.h"
#include "WiFiMulti.h"
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <arduino-timer.h>  // https://www.arduino.cc/reference/en/libraries/arduino-timer/

// Digital I/O used
#define I2S_DOUT 15
#define I2S_BCLK 4
#define I2S_LRC 5

Audio audio;
String ssid = "mculab"; // Replace to your WiFi ssid
String password = "24923150"; // Replace to your WiFi password

#define EOT 0x04
#define HMI Serial2
#define HMI_CMD(...) \
  { \
    HMI.printf(__VA_ARGS__); \
    HMI.write(EOT); \
  }

// Define image ID BunHMI
#define IMG_ID_PLAY 1
#define IMG_ID_STOP 5

int playlist_len = 0;
bool play_loop = false;
// Disp volume contrl panel
bool disp_volpanel = false;
// PlayList index
int play_index = 0;
int durationTime = 0;


auto timer = timer_create_default();
const char *MP3_URL = "http://192.168.50.100:8880/mp3/";
const char *PlayList[] = {
  "Beyond.mp3",   
  "EndSummer.mp3",   
  "HolyNight.mp3",
  "Olsen.mp3",
  "SnowyPeaks.mp3",
  "Chiapas.mp3",
  "GoldenCage.mp3",
  "JackBox.mp3",
  "SetMatch.mp3"
};

// Play list

/**
Run tick for every 100ms
*/
bool timer_tick(void *) {
  if (audio.isRunning()) {
    int durTime = audio.getAudioFileDuration();
    if (durTime > 0) {
      if (durationTime == 0) {
        durationTime = durTime;
        HMI_CMD("playTime.range(0,%d)", durationTime);
      }
      int curTime = audio.getAudioCurrentTime();
      HMI_CMD("playTime.val(%d); labDur.text(\"%d/%d\")", curTime, curTime, durationTime);      
    }
  }
  return true;
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  HMI.begin(115200);
  HMI.setTimeout(10);
  // Wait HMI bootup
  delay(100);
  // Send dummy cmd to HMI
  HMI.printf("\x04\x04");
  // flush serial rx buffer
  char buff[256];
  int rxlen;
  do {
    rxlen = HMI.readBytes(buff, sizeof(buff));
  } while (rxlen > 0);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  int init_vol = 12;
  audio.setVolume(init_vol);  // 0...21
  // Reset UI status
  HMI_CMD("pl.options(\"\");playTime.val(0);cbloop.checked(0)");
  HMI_CMD("vol.range(0,21);vol.val(%d)", init_vol);
  HMI_CMD("imgPlay.src(%d)", IMG_ID_PLAY);
  // update playlist widget
  playlist_len = sizeof(PlayList) / sizeof(PlayList[0]);
  buff[0] = 0;
  for (int i = 0; i < playlist_len; i++) {
    if (i != 0) {
      strncat(buff, "\\n", sizeof(buff) - strlen(buff));
    }
    strncat(buff, PlayList[i], sizeof(buff) - strlen(buff));
  }
  HMI_CMD("pl.options(\"%s\")", buff);
  play_index = 0;

  // Init wifi
  HMI_CMD("labstat.text(\"Start Wifi...\")");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  int cnt = 0;
  // Connect to wifi AP
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    HMI_CMD("labstat.text(\"Wifi Conn:%d...\")", (cnt++) / 10);
  }
  HMI_CMD("labstat.text(\"Wifi Linked\")");

  timer.every(100, timer_tick);
}

/**
  * Description: Receive Data from HMI
  * Parameters:
  *  dat: Data buffer
  *  dat_len: Data buffer length
  * Return:
  *  length of rx bytes
*/
int rxHmiData(char *dat, int dat_len) {
  static char hmiBuff[256];
  static uint8_t hmiBuffLen = 0;

  if (!HMI.available()) {
    return 0;
  }
  int rxlen = HMI.readBytes(hmiBuff + hmiBuffLen, sizeof(hmiBuff) - hmiBuffLen);
  int i;
  hmiBuffLen += rxlen;
  for (i = hmiBuffLen - 1; i >= 0; i--) {
    if (hmiBuff[i] == EOT) {
      // Got EOT Byte
      hmiBuff[i++] = 0;  // Change EOT to NULL,  string  terminate
      int hmi_len = (i < dat_len) ? i : dat_len;
      // Copy hmiBuff to dat
      memcpy(dat, hmiBuff, hmi_len);
      // Move remain data to the head of hmiBuff
      int remain_len = 0;
      while (i < hmiBuffLen) {
        hmiBuff[remain_len] = hmiBuff[i];
        remain_len++;
        i++;
      }
      hmiBuffLen = remain_len;
      return hmi_len;
    }
  }
  return 0;
}

// Start play music
static void playMusic(int index)
{
  char buff[256];
  audio.stopSong();
  snprintf(buff, sizeof(buff), "%s%s", MP3_URL, PlayList[index]);
  audio.connecttohost(buff);  
  durationTime = 0;
}

// String from BunHMI ptr cmd
const char *LOOP_ = "LOOP:";
const char *VOLSW_ = "VOLSW:";
const char *PL_ = "PL:";
const char *VOL_ = "VOL:";
const char *PLAY_ = "PLAY:";
const char *NEXT_ = "NEXT:";
const char *PREV_ = "PREV:";
const char *PAUSE_ = "PAUSE:";
const char *SPK_ = "SPK:";

/**
HMI Data handler
*/
void handleHmiData(const char *dat) {
  int val;
  char buff[256];

  //=========== Handle loop ======================
  if (strncmp(dat, LOOP_, strlen(LOOP_)) == 0) {
    val = strtoul(dat + strlen(LOOP_), NULL, 0);
    audio.setFileLoop(val ? true : false);
    return;
  }
  //=========== Handle PL ======================
  if (strncmp(dat, PL_, strlen(PL_)) == 0) {
    play_index = strtoul(dat + strlen(PL_), NULL, 0);
    Serial.printf("pl:%d\n", play_index);
    return;
  }

//=========== Handle VOLSW ======================
  if (strncmp(dat, VOLSW_, strlen(VOLSW_)) == 0) {
    if (disp_volpanel) {
      // Hide panVol
      HMI_CMD("panVol.anim_y(190, 200, 0,1)");
    } else {
      // Show panVol
      HMI_CMD("panVol.anim_y(130, 200, 0,1)");
    }
    disp_volpanel = !disp_volpanel;
    return;
  }

  //=========== Handle VOL ======================
  if (strncmp(dat, VOL_, strlen(VOL_)) == 0) {
    if (!audio.isRunning()) {
      return;
    }
    val = strtoul(dat + strlen(VOL_), NULL, 0);
    audio.setVolume(val); 
    return;
  }

//=========== Handle PLAY ======================
  if (strncmp(dat, PLAY_, strlen(PLAY_)) == 0) {
    if (audio.isRunning()) {
      audio.stopSong();
      val = IMG_ID_PLAY;
    } else {
      playMusic(play_index);
      val = IMG_ID_STOP;
    }
    HMI_CMD("imgPlay.src(%d)", val);
    return;
  }

  if (strncmp(dat, SPK_, strlen(SPK_)) == 0) {
    const char* tts = dat + strlen(SPK_);
    if(strlen(tts) > 0){
      audio.stopSong();
      audio.connecttospeech(tts, "en");
    }
  }

  //=========== Handle NEXT ======================
  if (strncmp(dat, NEXT_, strlen(NEXT_)) == 0) {
    if (!audio.isRunning()) {
      return;
    }
    if(++play_index >= playlist_len){
      play_index = 0;
    }
    playMusic(play_index);        
    HMI_CMD("pl.selected(%d,1)", play_index);    
    return;
  }

  //=========== Handle NEXT ======================
  if (strncmp(dat, PREV_, strlen(PREV_)) == 0) {
    if (!audio.isRunning()) {
      return;
    }
    if(play_index == 0){
      play_index = playlist_len;
    }
    --play_index;
    playMusic(play_index);    
    HMI_CMD("pl.selected(%d,1)", play_index);    
    return;
  }

    //=========== Handle PAUSE ======================
  if (strncmp(dat, PAUSE_, strlen(PAUSE_)) == 0) {
    // Only work for webstream or local file
    audio.pauseResume();    
    return;
  }
}


void loop() {
  char rxbuff[256];
  int rxlen;

  timer.tick();
  audio.loop();
  // Check Data from HMI
  rxlen = rxHmiData(rxbuff, sizeof(rxbuff));
  if (rxlen) {
    // Handle data from HMI
    handleHmiData(rxbuff);
  }



  // if(Serial.available()){ // put streamURL in serial monitor
  //     audio.stopSong();
  //     String r=Serial.readString(); r.trim();
  //     if(r.length()>5) audio.connecttohost(r.c_str());
  //     log_i("free heap=%i", ESP.getFreeHeap());
  // }
}

// // optional
void audio_info(const char *info) {
  Serial.print("info        ");
  Serial.println(info);
}
void audio_id3data(const char *info) {  //id3 metadata
  Serial.print("id3data     ");
  Serial.println(info);
}
void audio_eof_mp3(const char *info) {  //end of file
  Serial.print("eof_mp3     ");
  Serial.println(info);
}
void audio_showstation(const char *info) {
  Serial.print("station     ");
  Serial.println(info);
}
void audio_showstreamtitle(const char *info) {
  Serial.print("streamtitle ");
  Serial.println(info);
}
void audio_bitrate(const char *info) {
  Serial.print("bitrate     ");
  Serial.println(info);
}
void audio_commercial(const char *info) {  //duration in sec
  Serial.print("commercial  ");
  Serial.println(info);
}
void audio_icyurl(const char *info) {  //homepage
  Serial.print("icyurl      ");
  Serial.println(info);
}
void audio_lasthost(const char *info) {  //stream URL played
  Serial.print("lasthost    ");
  Serial.println(info);
}

void audio_eof_stream(const char *info) {
  HMI_CMD("imgPlay.src(%d)", IMG_ID_PLAY);
  Serial.print("EOS:      ");
  Serial.println(info);
}
