#include "SD_MMC.h" //SDCARD

#include "driver/i2s.h"
#define SDMMC_CLK   39
#define SDMMC_CMD   38
#define SDMMC_D0    40  

//ubah pin ini bila beda gpio
#define I2S_MIC_SERIAL_CLOCK 3
#define I2S_MIC_LEFT_RIGHT_CLOCK 1
#define I2S_MIC_SERIAL_DATA 14

//setting wav
#define SAMPLE_BUFFER_SIZE 1024//LOWER THIS IF CRASH / cut on audio
#define SAMPLE_RATE 48000
#define BITRATE 16
#define CHANNEL 1;

// don't mess around with this
i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// and don't mess around with this
i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA};

void CreateWavHeader(byte* header, int waveDataSize){
  uint16_t channels = CHANNEL;
  uint32_t sampleRate = SAMPLE_RATE;
  uint16_t bitsPerSample = BITRATE;
  uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  header[0] = 'R';
  header[1] = 'I';
  header[2] = 'F';
  header[3] = 'F';
  unsigned int fileSizeMinus8 = waveDataSize + 44 - 8;
  header[4] = (byte)(fileSizeMinus8 & 0xFF);
  header[5] = (byte)((fileSizeMinus8 >> 8) & 0xFF);
  header[6] = (byte)((fileSizeMinus8 >> 16) & 0xFF);
  header[7] = (byte)((fileSizeMinus8 >> 24) & 0xFF);
  header[8] = 'W';
  header[9] = 'A';
  header[10] = 'V';
  header[11] = 'E';
  header[12] = 'f';
  header[13] = 'm';
  header[14] = 't';
  header[15] = ' ';
  header[16] = 0x10;  // linear PCM
  header[17] = 0x00;
  header[18] = 0x00;
  header[19] = 0x00;
  header[20] = 0x01;  // linear PCM
  header[21] = 0x00;
  header[22] = 0x01;  // monoral
  header[23] = 0x00;
  header[24] = (sampleRate & 0xFF); //sample rate
  header[25] = ((sampleRate >> 8) & 0xFF);
  header[26] = ((sampleRate >> 16) & 0xFF);
  header[27] = ((sampleRate >> 24) & 0xFF);
  header[28] = (byteRate & 0xFF); //byte rate
  header[29] = ((byteRate >> 8) & 0xFF);
  header[30] = ((byteRate >> 16) & 0xFF);
  header[31] = ((byteRate >> 24) & 0xFF);
  header[32] = 0x02;  // 16bit monoral
  header[33] = 0x00;
  header[34] = 0x10;  // 16bit
  header[35] = 0x00;
  header[36] = 'd';
  header[37] = 'a';
  header[38] = 't';
  header[39] = 'a';
  header[40] = (byte)(waveDataSize & 0xFF);
  header[41] = (byte)((waveDataSize >> 8) & 0xFF);
  header[42] = (byte)((waveDataSize >> 16) & 0xFF);
  header[43] = (byte)((waveDataSize >> 24) & 0xFF);
}

int lastfilename;
void SD_init(){
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_D0);
  //Set sd speed to 20Mhz, if not set not gonna work in ESP32-s3, try to get it back to 40Mhz if possible
  if(!SD_MMC.begin("/sdcard", true, false, 20000, 5)){
    Serial.println("Card Mount Failed");
    return;
  }
  else{//make sure you have already have tracker.txt on sdcard, with number in it
    File file = SD_MMC.open("/tracker.txt");
    
    if (file) {
      if (file) {
          if (file.available()) {
              lastfilename = file.parseInt();
          }
          file.close();
      } else {
          Serial.println("Gagal membuka file untuk dibaca.");
      }
      Serial.print("Nomor file terakhir ");
      Serial.println(lastfilename);
    }


    uint8_t cardType = SD_MMC.cardType();
    if(cardType == CARD_NONE){
        Serial.println("No SD_MMC card attached");
        return;
    }

    Serial.print("SD_MMC Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    //SD Size
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
  }
}

int record_time = 10;//seconds
//total filesize
int waveDataSize = record_time * SAMPLE_RATE * BITRATE * 1 / 8;
int recode_time = millis();
int part_time = recode_time;

File file;
void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  SD_init();
  
  
  String filename = "/sound.wav";
  SD_MMC.remove(filename);
  file = SD_MMC.open(filename, FILE_WRITE);
  if(!file){
    Serial.println("ERROR!");

  }
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2s_mic_pins);
  Serial.println("Begin to record:");
  int32_t buff[SAMPLE_BUFFER_SIZE];
  for (int j = 0; j < waveDataSize / sizeof(buff); ++j)//loop until datasize acquired!
    
    { 
      size_t bytes_read = 0;
      i2s_read(I2S_NUM_0, buff, sizeof(int32_t) * SAMPLE_BUFFER_SIZE, &bytes_read, portMAX_DELAY);
      // int samplesRead = bytes_read / sizeof(int32_t);
      // for (int i = 0; i < samplesRead; i++) {
      // buff[i] = (int32_t)(buff[i] * GAIN);
      // failed gain} 
      file.write((const byte *)buff, sizeof(buff));
      Serial.print(millis()-recode_time);
      Serial.println(" ms");
      recode_time = millis();
    }
  file.seek(0);//back to start of the file to write header
  int headerSize = 44;
  byte header[headerSize];
  CreateWavHeader(header, waveDataSize);
  file.write(header, headerSize);
  Serial.println("Finish");
  file.close();
}

void loop() {
  // put your main code here, to run repeatedly:

}
