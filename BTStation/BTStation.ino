//#define DEBUG

#include <Wire.h>
#include "ds3231.h"
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <SPIFlash.h>

#ifdef DEBUG
#include <SoftwareSerial.h>
#endif

#define UART_SPEED 9600

#define FW_VERSION						104 //версия прошивки, номер пишется в чипы

#ifdef DEBUG
#define DEBUG_RX						2 //
#define DEBUG_TX						6 //
#endif

#define BUZZER_PIN						3 //пищалка
#define LED_PIN							4 //светодиод
#define RTC_ENABLE_PIN					5 //питание часов кроме батарейного

#define FLASH_ENABLE_PIN				7 //SPI enable pin
#define FLASH_SS_PIN					8 //SPI SELECT pin
#define RFID_RST_PIN					9 //рфид модуль reset
#define RFID_SS_PIN						10 //рфид модуль chip_select
//#define RFID_MOSI_PIN					11 //рфид модуль
//#define RFID_MISO_PIN					12 //рфид модуль
//#define RFID_SCK_PIN					13 //рфид модуль
#define BATTERY_PIN						A0 //замер напряжения батареи

#define EEPROM_STATION_NUMBER_ADDRESS	00 //номер станции в eeprom памяти
#define EEPROM_STATION_MODE_ADDRESS		10 //номер режима в eeprom памяти
#define EEPROM_VOLTAGE_KOEFF			20 //коэфф. пересчета значения ADC в вольты = 0,00587

//команды
#define COMMAND_SET_MODE			0x80
#define COMMAND_SET_TIME			0x81
#define COMMAND_RESET_STATION		0x82
#define COMMAND_GET_STATUS			0x83
#define COMMAND_INIT_CHIP			0x84
#define COMMAND_GET_LAST_TEAMS		0x85
#define COMMAND_GET_TEAM_RECORD		0x86
#define COMMAND_READ_CARD_PAGE		0x87
#define COMMAND_UPDATE_TEAM_MASK	0x88
#define COMMAND_WRITE_CARD_PAGE		0x89
#define COMMAND_READ_FLASH			0x8a
#define COMMAND_WRITE_FLASH			0x8b
#define COMMAND_ERASE_FLASH_SECTOR	0x8c
#define COMMAND_GET_CONFIG			0x8d
#define COMMAND_SET_KOEFF			0x8e

//размеры данных для команд
#define DATA_LENGTH_SET_MODE			1
#define DATA_LENGTH_SET_TIME			6
#define DATA_LENGTH_RESET_STATION		7
#define DATA_LENGTH_GET_STATUS			0
#define DATA_LENGTH_INIT_CHIP			4
#define DATA_LENGTH_GET_LAST_TEAMS		0
#define DATA_LENGTH_GET_TEAM_RECORD		2
#define DATA_LENGTH_READ_CARD_PAGE		2
#define DATA_LENGTH_UPDATE_TEAM_MASK	8
#define DATA_LENGTH_WRITE_CARD_PAGE		13
#define DATA_LENGTH_READ_FLASH			8
#define DATA_LENGTH_WRITE_FLASH			4  //and more according to data length
#define DATA_LENGTH_ERASE_FLASH_SECTOR	2
#define DATA_LENGTH_GET_CONFIG			0
#define DATA_LENGTH_SET_KOEFF			4

//ответы станции
#define REPLY_SET_MODE				0x90
#define REPLY_SET_TIME				0x91
#define REPLY_RESET_STATION			0x92
#define REPLY_GET_STATUS			0x93
#define REPLY_INIT_CHIP				0x94
#define REPLY_GET_LAST_TEAMS		0x95
#define REPLY_GET_TEAM_RECORD		0x96
#define REPLY_READ_CARD_PAGE		0x97
#define REPLY_UPDATE_TEAM_MASK		0x98
#define REPLY_WRITE_CARD_PAGE		0x99
#define REPLY_READ_FLASH			0x9a
#define REPLY_WRITE_FLASH			0x9b
#define REPLY_ERASE_FLASH_SECTOR	0x9c
#define REPLY_GET_CONFIG			0x9d
#define REPLY_SET_KOEFF				0x9e

//режимы станции
#define MODE_INIT		0
#define MODE_START_KP	1
#define MODE_FINISH_KP	2

//коды ошибок станции
#define OK				0
#define WRONG_STATION	1
#define READ_ERROR		2
#define WRITE_ERROR		3
#define LOW_INIT_TIME	4
#define WRONG_CHIP		5
#define NO_CHIP			6
#define BUFFER_OVERFLOW	7
#define WRONG_DATA		8
#define WRONG_UID		9
#define WRONG_TEAM		10
#define NO_DATA			11
#define WRONG_COMMAND	12
#define ERASE_ERROR		13

//страницы в чипе. 0-7 служебные, 8-... для отметок
#define PAGE_UID		0
#define PAGE_CHIP_NUM	4 //номер_чипа + тип_чипа + версия_прошивки
#define PAGE_INIT_TIME	5 //время инициализации
#define PAGE_TEAM_MASK	6
#define PAGE_RESERVED2	7
#define PAGE_DATA_START	8

//параметры чипов
//#define NTAG213_MAX_PAGE	40 //максимальное число страниц на чипе
//#define NTAG215_MAX_PAGE	130 //максимальное число страниц на чипе
#define TAG_MAX_PAGE	130 //максимальное число страниц на чипе
//#define NTAG216_MAX_PAGE	226 //максимальное число страниц на чипе

#define NTAG_TYPE			215 //режим работы и тип чипа

uint16_t LOG_LENGTH = 4000; //максимальное кол-во записей в логе

#define LOG_RECORD_LENGTH	1024 //размер записи лога (на 1 чип)

//описание протокола
#define STATION_NUMBER_BYTE	3
#define LENGTH_BYTE			4
#define COMMAND_BYTE		5
#define DATA_START_BYTE		6

#define receiveTimeOut 1000

//размер буфера последних команд
const uint8_t lastTeamsLength = 10;
//станция запоминает последние команды сюда
uint8_t lastTeams[lastTeamsLength * 2];
uint32_t lastTimeChecked = 0;
uint16_t totalChipsChecked = 0; // количество отмеченных чипов в памяти.

//по умолчанию номер станции и режим.
uint8_t stationNumber = 0;
uint8_t stationMode = MODE_INIT;
const uint32_t maxTimeInit = 600000UL; //одна неделя

float voltageKoeff = 0.00578;

uint8_t ntag_page[16]; //буфер для чтения из карты через ntagRead4pages()

SPIFlash SPIflash(FLASH_SS_PIN); //флэш-память

//рфид-модуль
MFRC522::StatusCode status;
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN); // Create MFRC522 instance

//хранение времени
struct ts systemTime; //time

//UART command buffer
uint8_t uartBuffer[256];
uint8_t uartBufferPosition = 0;
bool uartReady = false;
uint32_t uartTimeout = 1000;
bool receivingData = false;
uint32_t receiveStartTime = 0;

//новая маска для замены в чипе
uint8_t newTeamMask[8];

#ifdef DEBUG
SoftwareSerial DebugSerial(DEBUG_RX, DEBUG_TX);
#endif


void setup()
{
	Serial.begin(UART_SPEED);
#ifdef DEBUG
	DebugSerial.begin(9600);
#endif

	analogReference(INTERNAL);

	pinMode(LED_PIN, OUTPUT);
	pinMode(BUZZER_PIN, OUTPUT);

	//даем питание на флэш
	pinMode(FLASH_ENABLE_PIN, OUTPUT);
	digitalWrite(FLASH_ENABLE_PIN, HIGH);
	delay(1);
	SPIflash.begin();

	//даем питание на часы
	pinMode(RTC_ENABLE_PIN, OUTPUT);
	digitalWrite(RTC_ENABLE_PIN, HIGH);
	delay(1);
	DS3231_init(DS3231_INTCN);
	DS3231_get(&systemTime);

	//читаем номер станции из памяти
	stationNumber = eepromread(EEPROM_STATION_NUMBER_ADDRESS); //Read the station number from the EEPROM
	if (stationNumber == 255 || stationNumber == -1)
	{
		stationNumber = 0;
	}

	//читаем номер режима из памяти
	int c = eepromread(EEPROM_STATION_MODE_ADDRESS);
	if (c == -1)
	{
		beep(200, 20);
	}
	if (c == MODE_START_KP) stationMode = MODE_START_KP;
	else if (c == MODE_FINISH_KP) stationMode = MODE_FINISH_KP;
	else stationMode = MODE_INIT;

	//читаем коэфф. пересчета напряжения

	//читаем номер станции из памяти
	union Convert
	{
		float number;
		byte byte[4];
	} p;
	for (byte i = 0; i < 4; i++)
	{
		p.byte[i] = eepromread(EEPROM_VOLTAGE_KOEFF + i * 3); //Read the station number from the EEPROM
	}
	voltageKoeff = p.number;

	LOG_LENGTH = (uint32_t)(SPIflash.getCapacity() / LOG_RECORD_LENGTH);

	totalChipsChecked = refreshChipCounter();

	beep(800, 1);
}

void loop()
{
	/*
	 * В цикле сначала считывааем данные из порта, если получили полный пакет,
	 * то ищем соответсвующую функцию и обабатываем её
	 * Если режим станци не нулевой, то станция работает также как кп - автоматически делает отметки на чипы
	 */

	 // check receive timeout
	if (receivingData && millis() - receiveStartTime > receiveTimeOut)
	{
#ifdef DEBUG
		//DebugSerial.println(F("receive timeout"));
#endif
		uartBufferPosition = 0;
		uartReady = false;
		receivingData = false;
	}

	// check UART for data
	if (Serial.available())
	{
		uartReady = readUart();
	}

	//обработать пришедшую команду
	if (uartReady)
	{
		uartReady = false;
		executeCommand();
	}

	//если режим КП то отметить чип автоматом
	if (stationMode != 0)
	{
		processRfidCard();
	}
}

//Command processing

//поиск функции
void executeCommand()
{
#ifdef DEBUG
	//DebugSerial.print(F("Command:"));
	//DebugSerial.println(String(uartBuffer[COMMAND_BYTE], HEX));
#endif
	bool errorLengthFlag = false;
	switch (uartBuffer[COMMAND_BYTE])
	{
	case COMMAND_SET_MODE:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_SET_MODE) setMode();
		else errorLengthFlag = true;
		break;
	case COMMAND_SET_TIME:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_SET_TIME) setTime();
		else errorLengthFlag = true;
		break;
	case COMMAND_RESET_STATION:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_RESET_STATION) resetStation();
		else errorLengthFlag = true;
		break;
	case COMMAND_GET_STATUS:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_GET_STATUS) getStatus();
		else errorLengthFlag = true;
		break;
	case COMMAND_INIT_CHIP:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_INIT_CHIP) initChip();
		else errorLengthFlag = true;
		break;
	case COMMAND_GET_LAST_TEAMS:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_GET_LAST_TEAMS) getLastTeams();
		else errorLengthFlag = true;
		break;
	case COMMAND_GET_TEAM_RECORD:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_GET_TEAM_RECORD) getTeamRecord();
		else errorLengthFlag = true;
		break;
	case COMMAND_READ_CARD_PAGE:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_READ_CARD_PAGE) readCardPages();
		else errorLengthFlag = true;
		break;
	case COMMAND_UPDATE_TEAM_MASK:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_UPDATE_TEAM_MASK) updateTeamMask();
		else errorLengthFlag = true;
		break;
	case COMMAND_WRITE_CARD_PAGE:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_WRITE_CARD_PAGE) writeCardPage();
		else errorLengthFlag = true;
		break;
	case COMMAND_READ_FLASH:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_READ_FLASH) readFlash();
		else errorLengthFlag = true;
		break;
	case COMMAND_WRITE_FLASH:
		if (uartBuffer[LENGTH_BYTE] >= DATA_LENGTH_WRITE_FLASH) writeFlash();
		else errorLengthFlag = true;
		break;
	case COMMAND_ERASE_FLASH_SECTOR:
		if (uartBuffer[LENGTH_BYTE] >= DATA_LENGTH_ERASE_FLASH_SECTOR) eraseFlashSector();
		else errorLengthFlag = true;
		break;
	case COMMAND_GET_CONFIG:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_GET_CONFIG) getConfig();
		else errorLengthFlag = true;
		break;
	case COMMAND_SET_KOEFF:
		if (uartBuffer[LENGTH_BYTE] == DATA_LENGTH_SET_KOEFF) setKoeff();
		else errorLengthFlag = true;
		break;
	}
	uartBufferPosition = 0;
	if (errorLengthFlag) sendError(WRONG_COMMAND, 0);

#ifdef DEBUG
	//if (errorLengthFlag) DebugSerial.println(F("Incorrect data length"));
#endif
}

//установка режима
void setMode()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_SET_MODE);
		return;
	}

	//0: новый номер режима
	stationMode = uartBuffer[DATA_START_BYTE];
	eepromwrite(EEPROM_STATION_MODE_ADDRESS, stationMode);

	//формирование пакета данных.
	init_package(REPLY_SET_MODE);

	//0: код ошибки
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_SET_MODE);
		return;
	}
	sendData();
}

//обновление времени на станции
void setTime()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_SET_MODE);
		return;
	}

	systemTime.year = uartBuffer[DATA_START_BYTE] + 2000;
	systemTime.mon = uartBuffer[DATA_START_BYTE + 1];
	systemTime.mday = uartBuffer[DATA_START_BYTE + 2];
	systemTime.hour = uartBuffer[DATA_START_BYTE + 3];
	systemTime.min = uartBuffer[DATA_START_BYTE + 4];
	systemTime.sec = uartBuffer[DATA_START_BYTE + 5];

#ifdef DEBUG
	//DebugSerial.print(F("Time: "));
	//for (uint8_t i = 0; i < 6; i++) DebugSerial.println(String(uartBuffer[DATA_START_BYTE + i]) + " ");
#endif

	//0-3: дата и время в unixtime
	//systemTime.unixtime = uartBuffer[DATA_START_BYTE] * 16777216 + uartBuffer[DATA_START_BYTE] * 65536 + uartBuffer[DATA_START_BYTE] * 256 + uartBuffer[DATA_START_BYTE];

	delay(1);
	DS3231_set(systemTime); //correct time

	DS3231_get(&systemTime);
	uint32_t tmpTime = systemTime.unixtime;

	init_package(REPLY_SET_TIME);

	//0: код ошибки
	//1-4: текущее время
	bool flag = true;
	flag &= addData(OK);
	flag &= addData((tmpTime & 0xFF000000) >> 24);
	flag &= addData((tmpTime & 0x00FF0000) >> 16);
	flag &= addData((tmpTime & 0x0000FF00) >> 8);
	flag &= addData(tmpTime & 0x000000FF);
	if (!flag)
	{
		sendError(BUFFER_OVERFLOW, REPLY_SET_TIME);
		return;
	}
	sendData();
}

//сброс настроек станции
void resetStation()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_RESET_STATION);
		return;
	}

	//0-1: кол-во отмеченных карт (для сверки)
	//2-5: время последней отметки(для сверки)
	//6 : новый номер станции
	//проверить количество отметок
	uint16_t checkCardNumber = uartBuffer[DATA_START_BYTE];
	checkCardNumber <<= 8;
	checkCardNumber += uartBuffer[DATA_START_BYTE + 1];

	if (checkCardNumber != totalChipsChecked)
	{
		sendError(WRONG_DATA, REPLY_RESET_STATION);
		return;
	}

	//проверить время последней отметки
	uint32_t checkLastTime = uartBuffer[DATA_START_BYTE + 2];
	checkLastTime <<= 8;
	checkLastTime += uartBuffer[DATA_START_BYTE + 3];
	checkLastTime <<= 8;
	checkLastTime += uartBuffer[DATA_START_BYTE + 4];
	checkLastTime <<= 8;
	checkLastTime += uartBuffer[DATA_START_BYTE + 5];
	if (checkLastTime != lastTimeChecked)
	{
		sendError(WRONG_DATA, REPLY_RESET_STATION);
		return;
	}

	stationMode = 0;
	eepromwrite(EEPROM_STATION_MODE_ADDRESS, stationMode);
	stationNumber = uartBuffer[DATA_START_BYTE + 6];
	eepromwrite(EEPROM_STATION_NUMBER_ADDRESS, stationNumber);

	SPIflash.eraseChip();

	lastTimeChecked = 0;
	totalChipsChecked = 0;

	init_package(REPLY_RESET_STATION);

	//0: код ошибки
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_RESET_STATION);
		return;
	}

	sendData();
}

// выдает статус: время на станции, номер станции, номер режима, число отметок, время последней страницы
void getStatus()
{
	DS3231_get(&systemTime);
	uint32_t tmpTime = systemTime.unixtime;

	//0: код ошибки
	//1 - 4: текущее время
	//5 - 6 : количество отметок на станции
	//7 - 10 : время последней отметки на станции
	//11 - 12 : напряжение батареи в условных единицах[0..1023] ~[0..1.1В]
	//13 - 14 : температура чипа DS3231(чуть выше окружающей среды)
	init_package(REPLY_GET_STATUS);

	bool flag = true;
	flag &= addData(OK);

	flag &= addData((tmpTime & 0xFF000000) >> 24);
	flag &= addData((tmpTime & 0x00FF0000) >> 16);
	flag &= addData((tmpTime & 0x0000FF00) >> 8);
	flag &= addData(tmpTime & 0x000000FF);

	flag &= addData((totalChipsChecked & 0xFF00) >> 8);
	flag &= addData(totalChipsChecked & 0x00FF);

	flag &= addData((lastTimeChecked & 0xFF000000) >> 24);
	flag &= addData((lastTimeChecked & 0x00FF0000) >> 16);
	flag &= addData((lastTimeChecked & 0x0000FF00) >> 8);
	flag &= addData(lastTimeChecked & 0x000000FF);

	uint16_t batteryLevel = getBatteryLevel();
	flag &= addData((batteryLevel & 0xFF00) >> 8);
	flag &= addData(batteryLevel & 0x00FF);

	int temperature = (int)DS3231_get_treg();
	flag &= addData((temperature & 0xFF00) >> 8);
	flag &= addData(temperature & 0x00FF);

	if (!flag)
	{
		sendError(BUFFER_OVERFLOW, REPLY_GET_STATUS);
		return;
	}

	sendData();
}

//инициализация чипа. временно отключена проверка UID
void initChip()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_INIT_CHIP);
		return;
	}

	SPI.begin();      // Init SPI bus
	mfrc522.PCD_Init();   // Init MFRC522

   // Look for new cards
	if (!mfrc522.PICC_IsNewCardPresent())
	{
		sendError(NO_CHIP, REPLY_INIT_CHIP);
		return;
	}
	// Select one of the cards
	else if (!mfrc522.PICC_ReadCardSerial())
	{
		sendError(NO_CHIP, REPLY_INIT_CHIP);
		return;
	}

	//инициализация сработает только если время инициализации записанное уже на чипе превышает неделю до текущего времени
	if (!ntagRead4pages(PAGE_INIT_TIME))
	{
		sendError(READ_ERROR, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}
	uint32_t initTime = ntag_page[0];
	initTime <<= 8;
	initTime += ntag_page[1];
	initTime <<= 8;
	initTime += ntag_page[2];
	initTime <<= 8;
	initTime += ntag_page[3];
	DS3231_get(&systemTime);
	if ((systemTime.unixtime - initTime) < maxTimeInit)
	{
		sendError(LOW_INIT_TIME, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}

	//0-1: номер команды
	//2-3 : маска участников
	//ПОСЛЕ ММБ!!! 4 - 11: UID чипа

	//проверяем UID карты
	/*if (!ntagRead4pages(PAGE_UID))
	{
		sendError(READ_ERROR, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}
	bool flag = true;
	for (uint8_t i = 0; i <= 7; i++)
	{
		if (ntag_page[i] != uartBuffer[DATA_START_BYTE + 4 + i])
		{
			flag = false;
			break;
		}
	}
	if (!flag)
	{
		sendError(WRONG_UID, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}*/

	//заполняем карту 0xFF
	uint8_t dataBlock[4] = { 255,255,255,255 };
	/*for (uint8_t page = PAGE_CHIP_NUM; page < NTAG215_MAX_PAGE; page++)
	{
		if (!ntagWritePage(dataBlock, page))
		{
			sendError(WRITE_ERROR, REPLY_INIT_CHIP);
			SPI.end();
			return;
		}
	}*/

	//заполняем карту 0x00
	for (uint8_t i = 0; i < 4; i++) dataBlock[i] = 0;
	for (uint8_t page = PAGE_CHIP_NUM; page < TAG_MAX_PAGE; page++)
	{
		if (!ntagWritePage(dataBlock, page))
		{
			sendError(WRITE_ERROR, REPLY_INIT_CHIP);
			SPI.end();
			return;
		}
	}

	//пишем данные на карту
	//номер команды, тип чипа, версия прошивки станции	
	dataBlock[0] = uartBuffer[DATA_START_BYTE];
	dataBlock[1] = uartBuffer[DATA_START_BYTE + 1];
	dataBlock[2] = NTAG_TYPE;
	dataBlock[3] = FW_VERSION;
	if (!ntagWritePage(dataBlock, PAGE_CHIP_NUM))
	{
		sendError(WRITE_ERROR, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}

	//пишем на карту текущее время
	uint32_t tmpTime = systemTime.unixtime;
	dataBlock[0] = (tmpTime & 0xFF000000) >> 24;
	dataBlock[1] = (tmpTime & 0x00FF0000) >> 16;
	dataBlock[2] = (tmpTime & 0x0000FF00) >> 8;
	dataBlock[3] = tmpTime & 0x000000FF;
	if (!ntagWritePage(dataBlock, PAGE_INIT_TIME))
	{
		sendError(WRITE_ERROR, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}

	//маска участников
	dataBlock[0] = uartBuffer[DATA_START_BYTE + 2];
	dataBlock[1] = uartBuffer[DATA_START_BYTE + 3];
	dataBlock[2] = 0;
	dataBlock[3] = 0;
	if (!ntagWritePage(dataBlock, PAGE_TEAM_MASK))
	{
		sendError(WRITE_ERROR, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}

	//получаем UID карты
	if (!ntagRead4pages(PAGE_UID))
	{
		sendError(READ_ERROR, REPLY_INIT_CHIP);
		SPI.end();
		return;
	}
	SPI.end();

	init_package(REPLY_INIT_CHIP);
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_INIT_CHIP);
		return;
	}

	//добавляем в ответ время инициализации
	bool flag = true;
	flag &= addData((tmpTime & 0xFF000000) >> 24);
	flag &= addData((tmpTime & 0x00FF0000) >> 16);
	flag &= addData((tmpTime & 0x0000FF00) >> 8);
	flag &= addData(tmpTime & 0x000000FF);
	if (!flag)
	{
		sendError(BUFFER_OVERFLOW, REPLY_INIT_CHIP);
		return;
	}

	//добавляем в ответ UID
	for (uint8_t i = 0; i <= 7; i++)
	{
		if (!addData(ntag_page[i]))
		{
			sendError(BUFFER_OVERFLOW, REPLY_INIT_CHIP);
			return;
		}
	}
	sendData();
}

//получить последнюю отметившуюся команду
void getLastTeams()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_GET_LAST_TEAMS);
		return;
	}

	init_package(REPLY_GET_LAST_TEAMS);

	bool flag = true;
	//0: код ошибки
	flag &= addData(OK);

	//номера последних команд
	for (uint8_t i = 0; i < lastTeamsLength * 2; i++)
	{
		flag &= addData(lastTeams[i]);
	}
	if (!flag)
	{
		sendError(BUFFER_OVERFLOW, REPLY_GET_LAST_TEAMS);
		return;
	}

	sendData();
}

//получаем запись о команде из флэша
void getTeamRecord()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_GET_TEAM_RECORD);
		return;
	}

	//0-1: какую запись
	uint16_t recordNumber = uartBuffer[DATA_START_BYTE];
	recordNumber <<= 8;
	recordNumber += uartBuffer[DATA_START_BYTE + 1];

	if (recordNumber < 1 || recordNumber >= LOG_LENGTH)
	{
		sendError(WRONG_TEAM, REPLY_GET_TEAM_RECORD);
		return;
	}

	init_package(REPLY_GET_TEAM_RECORD);
	//0: код ошибки                            
	if (!addData(OK))
	{

		return;
	}

	//если ячейка лога пуста
	if (!readTeamFromFlash(recordNumber))
	{
		sendError(NO_DATA, REPLY_GET_TEAM_RECORD);
		return;
	}

	//1-2: номер команды
	//3-6 : время инициализации
	//7-8: маска команды	
	//9-12 : время последней отметки на станции
	//13: счетчик сохраненных страниц
	bool flag = true;
	for (uint8_t i = 0; i < 13; i++)
	{
		flag &= addData(ntag_page[i]);
	}

	if (!flag)
	{
		sendError(BUFFER_OVERFLOW, REPLY_GET_LAST_TEAMS);
		return;
	}

	sendData();
}

//снимаем постраничный дамп с карты.
void readCardPages()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_READ_CARD_PAGE);
		return;
	}

	SPI.begin();      // Init SPI bus
	mfrc522.PCD_Init();   // Init MFRC522
	// Look for new cards
	if (!mfrc522.PICC_IsNewCardPresent())
	{
		sendError(NO_CHIP, REPLY_READ_CARD_PAGE);
		SPI.end();
		return;
	}
	// Select one of the cards
	if (!mfrc522.PICC_ReadCardSerial())
	{
		sendError(NO_CHIP, REPLY_READ_CARD_PAGE);
		SPI.end();
		return;
	}

	uint8_t pageFrom = uartBuffer[DATA_START_BYTE];
	uint8_t pageTo = uartBuffer[DATA_START_BYTE + 1];

	init_package(REPLY_READ_CARD_PAGE);

	//0: код ошибки
	//1-8: UID чипа
	//9-12: данные из страницы карты(4 байта)
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_READ_CARD_PAGE);
		return;
	}

	if (!ntagRead4pages(PAGE_UID))
	{
		sendError(READ_ERROR, REPLY_READ_CARD_PAGE);
		SPI.end();
		return;
	}
	for (uint8_t i = 0; i <= 7; i++)
	{
		if (!addData(ntag_page[i]))
		{
			sendError(BUFFER_OVERFLOW, REPLY_READ_CARD_PAGE);
			return;
		}
	}

	while (pageFrom <= pageTo)
	{
		//0: какую страницу карты
		if (!ntagRead4pages(pageFrom))
		{
			sendError(READ_ERROR, REPLY_READ_CARD_PAGE);
			SPI.end();
			return;
		}
		uint8_t n = (pageTo - pageFrom + 1);
		if (n > 4) n = 4;
		for (uint8_t i = 0; i < n; i++)
		{
			if (!addData(pageFrom))
			{
				sendError(BUFFER_OVERFLOW, REPLY_READ_CARD_PAGE);
				return;
			}
			for (uint8_t j = 0; j < 4; j++)
			{
				if (!addData(ntag_page[i * 4 + j]))
				{
					sendError(BUFFER_OVERFLOW, REPLY_READ_CARD_PAGE);
					return;
				}

			}
			pageFrom++;
		}
	}
	SPI.end();

	sendData();
}

//Обновить маску команды в буфере
void updateTeamMask()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_UPDATE_TEAM_MASK);
		return;
	}

	//0-1: номер команды
	//2-5: время выдачи чипа
	//6-7: маска участников

	if (stationMode != 0)
	{
		for (uint8_t i = 0; i < 8; i++)
		{
			newTeamMask[i] = uartBuffer[DATA_START_BYTE + i];
		}
	}
	else
	{
		SPI.begin();      // Init SPI bus
		mfrc522.PCD_Init();   // Init MFRC522

		// Look for new cards
		if (!mfrc522.PICC_IsNewCardPresent())
		{
			sendError(NO_CHIP, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}

		// Select one of the cards
		if (!mfrc522.PICC_ReadCardSerial())
		{
			sendError(NO_CHIP, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}

		//проверить номер команды
		if (!ntagRead4pages(PAGE_CHIP_NUM))
		{
			sendError(READ_ERROR, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}
		if (ntag_page[0] != uartBuffer[DATA_START_BYTE] || ntag_page[1] != uartBuffer[DATA_START_BYTE + 1])
		{
			sendError(WRONG_TEAM, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}

		//проверить время выдачи чипа
		if (!ntagRead4pages(PAGE_INIT_TIME))
		{
			sendError(READ_ERROR, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}

		if (ntag_page[0] != uartBuffer[DATA_START_BYTE + 2]
			|| ntag_page[1] != uartBuffer[DATA_START_BYTE + 3]
			|| ntag_page[2] != uartBuffer[DATA_START_BYTE + 4]
			|| ntag_page[3] != uartBuffer[DATA_START_BYTE + 5])
		{
#ifdef DEBUG
			/*DebugSerial.println(F("chip init time wrong"));
			DebugSerial.print(String(ntag_page[0], HEX));
			DebugSerial.print(F("="));
			DebugSerial.println(String(uartBuffer[DATA_START_BYTE + 2], HEX));

			DebugSerial.print(F("upd mask chip error"));
			DebugSerial.print(String(ntag_page[1], HEX));
			DebugSerial.print(F("="));
			DebugSerial.println(String(uartBuffer[DATA_START_BYTE + 3], HEX));

			DebugSerial.print(F("upd mask chip error"));
			DebugSerial.print(String(ntag_page[2], HEX));
			DebugSerial.print(F("="));
			DebugSerial.println(String(uartBuffer[DATA_START_BYTE + 4], HEX));

			DebugSerial.print(F("upd mask chip error"));
			DebugSerial.print(String(ntag_page[3], HEX));
			DebugSerial.print(F("="));
			DebugSerial.println(String(uartBuffer[DATA_START_BYTE + 5], HEX));*/
#endif
			sendError(WRONG_CHIP, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}

		//записать страницу на карту
		uint8_t dataBlock[4] = { uartBuffer[DATA_START_BYTE + 6],uartBuffer[DATA_START_BYTE + 7], ntag_page[6],ntag_page[7] };
		if (!ntagWritePage(dataBlock, PAGE_TEAM_MASK))
		{
			sendError(WRITE_ERROR, REPLY_UPDATE_TEAM_MASK);
			SPI.end();
			return;
		}
		SPI.end();

		init_package(REPLY_UPDATE_TEAM_MASK);
	}

	//0: код ошибки
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_UPDATE_TEAM_MASK);
		return;
	}
	sendData();
}

//пишем присланные с ББ 4 байта в указанную страницу
void writeCardPage()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_WRITE_CARD_PAGE);
		return;
	}

	SPI.begin();      // Init SPI bus
	mfrc522.PCD_Init();   // Init MFRC522

	// Look for new cards
	if (!mfrc522.PICC_IsNewCardPresent())
	{
		sendError(NO_CHIP, REPLY_WRITE_CARD_PAGE);
		SPI.end();
		return;
	}

	// Select one of the cards
	if (!mfrc522.PICC_ReadCardSerial())
	{
		sendError(NO_CHIP, REPLY_WRITE_CARD_PAGE);
		SPI.end();
		return;
	}

	//0-7: UID чипа
	//8: номер страницы
	//9-12: данные для записи (4 байта)

	//проверить UID
	if (!ntagRead4pages(PAGE_UID))
	{
		sendError(READ_ERROR, REPLY_WRITE_CARD_PAGE);
		SPI.end();
		return;
	}
	bool flag = false;
	for (uint8_t i = 0; i <= 7; i++)
	{
		if (ntag_page[i] != uartBuffer[DATA_START_BYTE + i])
		{
			flag = true;
			break;
		}
	}
	if (flag)
	{
		sendError(WRONG_UID, REPLY_WRITE_CARD_PAGE);
		SPI.end();
		return;
	}

	//записать страницу
	uint8_t dataBlock[] =
	{
		uartBuffer[DATA_START_BYTE + 9],
		uartBuffer[DATA_START_BYTE + 10],
		uartBuffer[DATA_START_BYTE + 11],
		uartBuffer[DATA_START_BYTE + 12]
	};
	if (!ntagWritePage(dataBlock, uartBuffer[DATA_START_BYTE + 8]))
	{
		sendError(WRITE_ERROR, REPLY_WRITE_CARD_PAGE);
		SPI.end();
		return;
	}
	SPI.end();

	init_package(REPLY_WRITE_CARD_PAGE);

	//0: код ошибки
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_WRITE_CARD_PAGE);
		return;
	}
	sendData();
}

//читаем флэш
void readFlash()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_READ_FLASH);
		return;
	}

	//0-3: адрес начала чтения
	//4-7: адрес конца чтения
	uint32_t startAddress = uartBuffer[DATA_START_BYTE];
	startAddress <<= 8;
	startAddress += uartBuffer[DATA_START_BYTE + 1];
	startAddress <<= 8;
	startAddress += uartBuffer[DATA_START_BYTE + 2];
	startAddress <<= 8;
	startAddress += uartBuffer[DATA_START_BYTE + 3];

	uint32_t endAddress = uartBuffer[DATA_START_BYTE + 4];
	endAddress <<= 8;
	endAddress += uartBuffer[DATA_START_BYTE + 5];
	endAddress <<= 8;
	endAddress += uartBuffer[DATA_START_BYTE + 6];
	endAddress <<= 8;
	endAddress += uartBuffer[DATA_START_BYTE + 7];

#ifdef DEBUG
	/*DebugSerial.print(F("flash read="));
	DebugSerial.print(String(startAddress));
	DebugSerial.print(F("-"));
	DebugSerial.println(String(endAddress));*/
#endif

	init_package(REPLY_READ_FLASH);
	//0: код ошибки
	//1...: данные из флэша
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_READ_FLASH);
		return;
	}
	for (; startAddress <= endAddress; startAddress++)
	{
		uint8_t b = SPIflash.readByte(startAddress);
		if (!addData(b))
		{
			sendError(BUFFER_OVERFLOW, REPLY_READ_FLASH);
			return;
		}
#ifdef DEBUG
		/*DebugSerial.print(String(i));
		DebugSerial.print("=");
		if (b < 0x10) DebugSerial.print(F("0"));
		DebugSerial.println(String(b, HEX));*/
#endif
	}

	sendData();
}

//пишем в флэш !!! сделать замену данных через стирание
bool writeFlash()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_WRITE_FLASH);
		return;
	}

	//0-3: адрес начала записи
	//1: кол-во записанных байт (для проверки)
	uint32_t startAddress = uartBuffer[DATA_START_BYTE];
	startAddress <<= 8;
	startAddress += uartBuffer[DATA_START_BYTE + 1];
	startAddress <<= 8;
	startAddress += uartBuffer[DATA_START_BYTE + 2];
	startAddress <<= 8;
	startAddress += uartBuffer[DATA_START_BYTE + 3];

	init_package(REPLY_WRITE_FLASH);

	uint32_t i = startAddress + uartBuffer[LENGTH_BYTE] - 4;
	uint8_t n = 0;
	while (startAddress < i)
	{
		if (!SPIflash.writeByte(startAddress, uartBuffer[DATA_START_BYTE + 4 + n]))
		{
			sendError(WRITE_ERROR, REPLY_WRITE_FLASH);
			return false;
		}
		startAddress++;
		n++;
	}

	//0: код ошибки
	//1: кол-во записанных байт (для проверки)
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_WRITE_FLASH);
		return false;
	}
	if (!addData(n))
	{
		sendError(BUFFER_OVERFLOW, REPLY_WRITE_FLASH);
		return false;
	}

	sendData();
}

//стираем сектор флэша 256 байт
void eraseFlashSector()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_ERASE_FLASH_SECTOR);
		return;
	}

	uint32_t sectordNumber = uartBuffer[DATA_START_BYTE];
	sectordNumber <<= 8;
	sectordNumber += uartBuffer[DATA_START_BYTE + 1];

	if (!SPIflash.eraseSector(sectordNumber))
	{
		sendError(ERASE_ERROR, REPLY_ERASE_FLASH_SECTOR);
		return;
	}

	init_package(REPLY_ERASE_FLASH_SECTOR);
	//0: код ошибки
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_ERASE_FLASH_SECTOR);
		return;
	}
	sendData();
}

// выдает статус: время на станции, номер станции, номер режима, число отметок, время последней страницы
void getConfig()
{
	//0: код ошибки
	//1: версия прошивки
	//2: номер режима
	//3: тип чипов (емкость разная, а распознать их программно можно только по ошибкам чтения "дальних" страниц)
	//4-7: емкость флэш - памяти
	//8-11: размер сектора флэш - памяти
	//12-15: коэффициент пересчета напряжения(float, 4 bytes) - просто умножаешь коэффициент на полученное в статусе число и будет температура
	init_package(REPLY_GET_CONFIG);

	bool flag = true;
	flag &= addData(OK);
	flag &= addData(FW_VERSION);
	flag &= addData(stationMode);
	flag &= addData(NTAG_TYPE);

	uint32_t n = SPIflash.getCapacity();
	flag &= addData((n & 0xFF000000) >> 24);
	flag &= addData((n & 0x00FF0000) >> 16);
	flag &= addData((n & 0x0000FF00) >> 8);
	flag &= addData(n & 0x000000FF);

	n = SPIflash.getMaxPage();
	flag &= addData((n & 0xFF000000) >> 24);
	flag &= addData((n & 0x00FF0000) >> 16);
	flag &= addData((n & 0x0000FF00) >> 8);
	flag &= addData(n & 0x000000FF);

	byte v[4];
	floatToByte(v, voltageKoeff);
	flag &= addData(v[0]);
	flag &= addData(v[1]);
	flag &= addData(v[2]);
	flag &= addData(v[3]);

	if (!flag)
	{
		sendError(BUFFER_OVERFLOW, REPLY_GET_CONFIG);
		return;
	}

	sendData();
}

void setKoeff()
{
	//Если номер станции не совпадает с присланным в пакете, то отказ
	if (stationNumber != uartBuffer[STATION_NUMBER_BYTE])
	{
		sendError(WRONG_STATION, REPLY_SET_KOEFF);
		return;
	}

	//0-3: коэфф.
	union Convert
	{
		float number;
		byte byte[4];
	} p;

	for (byte i = 0; i < 4; i++)
	{
		p.byte[i] = uartBuffer[DATA_START_BYTE + i];
		eepromwrite(EEPROM_VOLTAGE_KOEFF + i * 3, uartBuffer[DATA_START_BYTE + i]); //Read the station number from the EEPROM
	}
	voltageKoeff = p.number;

	init_package(REPLY_SET_KOEFF);
	//0: код ошибки
	//1...: данные из флэша
	if (!addData(OK))
	{
		sendError(BUFFER_OVERFLOW, REPLY_SET_KOEFF);
		return;
	}

	sendData();
}

//Обработка поднесенного чипа
void processRfidCard()
{
	DS3231_get(&systemTime);
	uint32_t tempT = systemTime.unixtime;

	//включаем SPI ищем карту вблизи. Если не находим выходим из функции чтения чипов
	SPI.begin();      // Init SPI bus
	mfrc522.PCD_Init();    // Init MFRC522

	// Look for new cards
	if (!mfrc522.PICC_IsNewCardPresent())
	{
		SPI.end();
		return;
	}

	// Select one of the cards
	if (!mfrc522.PICC_ReadCardSerial())
	{
		SPI.end();
		return;
	}

	//читаем блок информации
	if (!ntagRead4pages(PAGE_CHIP_NUM))
	{
		SPI.end();
		return;
	}

	/*
	Фильтруем
	1 - чипы с командой №0 или > LOG_LENGTH
	2 - чип, который совпадает с уже отмеченным (в lastTeams[])
	3 - чип более недельной давности инициализации
	*/

	//Не слишком ли старый чип? Недельной давности и более
	uint32_t timeInit = ntag_page[4];
	timeInit = timeInit << 8;
	timeInit += ntag_page[5];
	timeInit = timeInit << 8;
	timeInit += ntag_page[6];
	timeInit = timeInit << 8;
	timeInit += ntag_page[7];
	if ((systemTime.unixtime - timeInit) > maxTimeInit)
	{
		SPI.end();
		return;
	}

	//Не равен ли номер чипа 0 или >= LOG_LENGTH
	uint16_t chipNum = (ntag_page[0] << 8) + ntag_page[1];
	if (chipNum < 1 || chipNum >= LOG_LENGTH)
	{
		SPI.end();
		return;
	}

	//не надо ли обновить у чипа маску?
	//0-1: номер команды
	//2-5: время выдачи чипа
	//6-7: маска участников
	if (ntag_page[0] == newTeamMask[0]
		&& ntag_page[1] == newTeamMask[1]
		&& ntag_page[4] == newTeamMask[2]
		&& ntag_page[5] == newTeamMask[3]
		&& ntag_page[6] == newTeamMask[4]
		&& ntag_page[7] == newTeamMask[5])
	{
		if ((ntag_page[8] != newTeamMask[6] || ntag_page[9] != newTeamMask[7]))
		{
			uint8_t dataBlock[4] = { newTeamMask[6], newTeamMask[7], ntag_page[10], ntag_page[11] };
			if (!ntagWritePage(dataBlock, PAGE_TEAM_MASK))
			{
				//sendError(WRITE_ERROR, REPLY_UPDATE_TEAM_MASK);
				SPI.end();
				return;
			}
			SPI.end();
		}
		return;
	}

	//сравнить с буфером последних команд
	bool flag = false;
	for (uint8_t i = 0; i < lastTeamsLength * 2; i = i + 2)
	{
		if (lastTeams[i] == ntag_page[0] && lastTeams[i + 1] == ntag_page[1])
		{
			flag = true;
			break;
		}
	}

	//Есть ли чип на флэше
	if (!flag && SPIflash.readByte((uint32_t)((uint32_t)chipNum * (uint32_t)LOG_RECORD_LENGTH)) != 255) flag = true;

	//если новый чип или финишный КП
	if (!flag || stationMode == MODE_FINISH_KP)
	{
		//ищем свободную страницу на карте
		uint8_t newPage = findNewPage();

		//ошибка чтения или больше максимума... Наверное, переполнен???
		if (newPage < PAGE_DATA_START || newPage >= TAG_MAX_PAGE)
		{
			SPI.end();
			return;
		}

		//Пишем на карту отметку
		if (!writeCheckPointToCard(newPage, tempT))
		{
			SPI.end();
			return;
		}

		//добавляем в буфер последних команд
		addLastTeam(chipNum);
		lastTimeChecked = tempT;
		if (!flag) totalChipsChecked++;

		//Пишем в лог карту
		writeDumpToFlash(chipNum, tempT);
		SPI.end();
		beep(200, 1);
#ifdef DEBUG
		DebugSerial.print(F("record# "));
		DebugSerial.println(String(chipNum));
#endif
	}
}

bool readUart()
{
	while (Serial.available())
	{
		int c = Serial.read();
		if (c == -1) // can't read stream
		{
#ifdef DEBUG
			//DebugSerial.println(F("read error"));
#endif
			uartBufferPosition = 0;
			receivingData = false;
			beep(50, 2);
			return false;
		}
		//0 byte = FE
		else if (uartBufferPosition == 0 && c == 0xfe)
		{
#ifdef DEBUG
			/*DebugSerial.print(F("byte0="));
			if (c < 0x10) DebugSerial.print(F("0"));
			DebugSerial.println(String(byte(c), HEX));*/
#endif
			receivingData = true;
			uartBuffer[uartBufferPosition] = (byte)c;
			uartBufferPosition++;
			// refresh timeout
			receiveStartTime = millis();
		}
		//1st byte = FE
		else if (uartBufferPosition == 1 && c == 0xfe)
		{
#ifdef DEBUG
			/*DebugSerial.print(F("byte1"));
			if (c < 0x10) DebugSerial.print(F("0"));
			DebugSerial.println(String(byte(c), HEX));*/
#endif
			uartBuffer[uartBufferPosition] = (byte)c;
			uartBufferPosition++;
		}
		//2nd byte = FE
		else if (uartBufferPosition == 2 && c == 0xfe)
		{
#ifdef DEBUG
			/*DebugSerial.print(F("byte2"));
			if (c < 0x10) DebugSerial.print(F("0"));
			DebugSerial.println(String(byte(c), HEX));*/
#endif
			uartBuffer[uartBufferPosition] = (byte)c;
			uartBufferPosition++;
		}
		//4th byte = command, length and data
		else if (uartBufferPosition >= STATION_NUMBER_BYTE)
		{
			uartBuffer[uartBufferPosition] = (byte)c;
#ifdef DEBUG
			/*DebugSerial.print(F("byte"));
			DebugSerial.print(String(uartBufferPosition));
			DebugSerial.print(F("="));
			if (c < 0x10) DebugSerial.print(F("0"));
			DebugSerial.println(String(byte(c), HEX));*/
#endif
			//incorrect length
			if (uartBufferPosition == LENGTH_BYTE && uartBuffer[LENGTH_BYTE] > (254 - DATA_START_BYTE))
			{
#ifdef DEBUG
				DebugSerial.println(F("incorrect length"));
#endif
				uartBufferPosition = 0;
				receivingData = false;
				beep(50, 2);
				return false;
			}

			//packet is received
			if (uartBufferPosition > LENGTH_BYTE && uartBufferPosition >= DATA_START_BYTE + uartBuffer[LENGTH_BYTE])
			{
				//crc matching
#ifdef DEBUG
				/*DebugSerial.print(F("received packet expected CRC="));
				DebugSerial.println(String(crcCalc(uartBuffer, STATION_NUMBER_BYTE, uartBufferPosition - 1), HEX));*/
#endif
				if (uartBuffer[uartBufferPosition] == crcCalc(uartBuffer, STATION_NUMBER_BYTE, uartBufferPosition - 1))
				{
#ifdef DEBUG
					/*DebugSerial.print(F("Command received:"));
					for (uint8_t i = 0; i <= uartBufferPosition; i++)
					{
						DebugSerial.print(F(" "));
						if (uartBuffer[i] < 0x10) DebugSerial.print(F("0"));
						DebugSerial.print(String(uartBuffer[i], HEX));
			}
					DebugSerial.println();*/
#endif
					uartBufferPosition = 0;
					receivingData = false;
					return true;
				}
				else // CRC not correct
				{
#ifdef DEBUG
					DebugSerial.println(F("incorrect crc"));
#endif
					uartBufferPosition = 0;
					receivingData = false;
					beep(50, 2);
					return false;
				}
			}
			uartBufferPosition++;
				}
		else
		{
#ifdef DEBUG
			DebugSerial.println(F("unexpected byte"));
#endif
			receivingData = false;
			uartBufferPosition = 0;
			beep(50, 3);
		}
	}
	return false;
			}

// Internal functions

//чтение заряда батареи
uint16_t getBatteryLevel()
{
	uint16_t AverageValue = 0;
	uint8_t MeasurementsToAverage = 16;
	for (uint8_t i = 0; i < MeasurementsToAverage; ++i)
	{
		AverageValue += analogRead(BATTERY_PIN);
		delay(1);
	}
	return AverageValue / MeasurementsToAverage;
}

//запись в память с мажоритальным резервированием
void eepromwrite(uint16_t adr, uint8_t val)
{
	for (uint8_t i = 0; i < 3; i++)
	{
		EEPROM.write(adr + i, val);
	}
}

//считывание из памяти с учетом мажоритального резервирования
int eepromread(uint16_t adr)
{
	uint8_t byte1 = EEPROM.read(adr);
	uint8_t byte2 = EEPROM.read(adr + 1);
	uint8_t byte3 = EEPROM.read(adr + 2);

	// возвращаем при совпадении два из трех
	if (byte1 == byte2 && byte1 == byte3)
	{
		return byte1;
	}
	else if (byte1 == byte2)
	{
		return byte1;
	}
	else if (byte1 == byte3)
	{
		return byte1;
	}
	else if (byte2 == byte3)
	{
		return byte2;
	}
	else
	{
		return -1;
	}

}

//сигнал станции, длительность сигнала и задержки в мс и число повторений
void beep(uint16_t ms, uint8_t n)
{
	for (uint8_t i = 0; i < n; i++)
	{
		digitalWrite(LED_PIN, HIGH);
		tone(BUZZER_PIN, 4000, ms);
		delay(ms);
		digitalWrite(LED_PIN, LOW);
		if ((n - i) != 0)
		{
			delay(ms);
		}
	}
}

//инициализация пакета данных
void init_package(uint8_t command)
{
	uartBuffer[0] = uartBuffer[1] = uartBuffer[2] = 0xFE;
	uartBuffer[3] = stationNumber;
	uartBuffer[COMMAND_BYTE] = command;
	uartBufferPosition = DATA_START_BYTE;
}

//добавление данных в буфер
bool addData(uint8_t data)
{
	if (uartBufferPosition > 255)
	{
		//sendError(BUFFER_OVERFLOW, uartBuffer[COMMAND_BYTE]);
		return false;
	}
	uartBuffer[uartBufferPosition] = data;
	uartBufferPosition++;
	return true;
}

//передача пакета данных.
void sendData()
{
	uartBuffer[LENGTH_BYTE] = uartBufferPosition - COMMAND_BYTE - 1;
	uartBuffer[uartBufferPosition] = crcCalc(uartBuffer, STATION_NUMBER_BYTE, uartBufferPosition - 1);
#ifdef DEBUG
	/*DebugSerial.print(F("Sending:"));
	for (uint8_t i = 0; i < uartBufferPosition; i++)
	{
		DebugSerial.print(F(" "));
		if (uartBuffer[i] < 0x10) DebugSerial.print(F("0"));
		DebugSerial.print(String(uartBuffer[i], HEX));
	}
	DebugSerial.println();*/
#endif
	Serial.write(uartBuffer, uartBufferPosition + 1);
	uartBufferPosition = 0;
}

//запись страницы (4 байта) в чип
bool ntagWritePage(uint8_t *dataBlock, uint8_t pageAdr)
{
	const uint8_t sizePageNtag = 4;
	status = (MFRC522::StatusCode) mfrc522.MIFARE_Ultralight_Write(pageAdr, dataBlock, sizePageNtag);
	if (status != MFRC522::STATUS_OK)
	{
		//SPI.end();
		return false;
	}

	uint8_t buffer[18];
	uint8_t size = sizeof(buffer);

	status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(pageAdr, buffer, &size);
	if (status != MFRC522::STATUS_OK)
	{
		//SPI.end();
		return false;
	}
	//SPI.end();

	for (uint8_t i = 0; i < 4; i++)
	{
		if (buffer[i] != dataBlock[i]) return false;
	}

	return true;
}

//чтение 4-х страниц (16 байт) из чипа
bool ntagRead4pages(uint8_t pageAdr)
{
	uint8_t size = 18;
	uint8_t buffer[18];

	status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(pageAdr, buffer, &size);
	if (status != MFRC522::STATUS_OK)
	{
		return false;
	}

	for (uint8_t i = 0; i < 16; i++)
	{
		ntag_page[i] = buffer[i];
	}
	return true;
}

//пишет на карту время и станцию отметки
bool writeCheckPointToCard(uint8_t newPage, uint32_t tempT)
{
	uint8_t dataBlock[4];
	dataBlock[0] = stationNumber;
	dataBlock[1] = (tempT & 0x00FF0000) >> 16;
	dataBlock[2] = (tempT & 0x0000FF00) >> 8;
	dataBlock[3] = (tempT & 0x000000FF);

	if (!ntagWritePage(dataBlock, newPage))
	{
		return false;
	}
	return true;
}

// !!! Поиск последней записанной страницы? Переписать эту черную магию...
uint8_t findNewPage()
{
	uint8_t page = PAGE_DATA_START;
	while (page < TAG_MAX_PAGE)
	{
		if (!ntagRead4pages(page))
		{
			return 0;
		}
		for (uint8_t n = 0; n < 4; n++)
		{
			//1) текущая страница пустая
			//2) равна номеру станции
			if (ntag_page[0 + n * 4] == 0 || ntag_page[0 + n * 4] == stationNumber) return page;
			//2) равна номеру станции, а след. страница пустая
			//if (ntag_page[0 + n * 4] == 0 || (ntag_page[0 + n * 4] == stationNumber && ntag_page[0 + 4 + n * 4] == 0)) return page;
			page++;
		}
	}
	return TAG_MAX_PAGE;
	/*uint8_t finishpage = NTAG215_MAX_PAGE - 2;
	uint8_t startpage = PAGE_DATA_START;
	uint8_t page = (finishpage + startpage) / 2;

	while (1)
	{
		if (finishpage == startpage)
		{
			return (finishpage);
		}

		page = (finishpage + startpage) / 2;

		if (!ntagRead4pages(page))
		{
			return 0;
		}

		if (ntag_page[0] == 0)
		{
			finishpage = (finishpage - startpage) / 2 + startpage;
		}
		else
		{
			startpage = finishpage - (finishpage - startpage) / 2;
		}
	}*/
}

//пишем дамп карты в лог !!! сделать замену данных через стирание
bool writeDumpToFlash(uint16_t recordNum, uint32_t tempT)
{
	//адрес хранения в каталоге
	uint32_t pageFlash = (uint32_t)(uint32_t(recordNum) * (uint32_t)LOG_RECORD_LENGTH);
#ifdef DEBUG
	DebugSerial.print(F("Flash address: "));
	DebugSerial.println(String(pageFlash));
#endif

	//если режим финишной станции, то, возможно, надо переписать содержимое.
	if (stationMode == MODE_FINISH_KP && SPIflash.readByte(pageFlash) != 255)
	{
		SPIflash.eraseSector((long)((float)pageFlash / 256));
#ifdef DEBUG
		DebugSerial.print(F("erased sector: "));
		DebugSerial.println(String((uint32_t)((uint32_t)pageFlash / (uint32_t)256)));
#endif
}

	//save basic parameters
	if (!ntagRead4pages(PAGE_CHIP_NUM))
	{
		return false;
}
	//1-2: номер команды
	SPIflash.writeByte(pageFlash, ntag_page[0]);
	SPIflash.writeByte(pageFlash + 1, ntag_page[1]);
	//3-6: время инициализации
	SPIflash.writeByte(pageFlash + 2, ntag_page[4]);
	SPIflash.writeByte(pageFlash + 3, ntag_page[5]);
	SPIflash.writeByte(pageFlash + 4, ntag_page[6]);
	SPIflash.writeByte(pageFlash + 5, ntag_page[7]);
	//7-8: маска команды
	SPIflash.writeByte(pageFlash + 6, ntag_page[8]);
	SPIflash.writeByte(pageFlash + 7, ntag_page[9]);
	//9-12: время последней отметки на станции
	SPIflash.writeByte(pageFlash + 8, (tempT & 0xFF000000) >> 24);
	SPIflash.writeByte(pageFlash + 9, (tempT & 0x00FF0000) >> 16);
	SPIflash.writeByte(pageFlash + 10, (tempT & 0x0000FF00) >> 8);
	SPIflash.writeByte(pageFlash + 11, tempT & 0x000000FF);
#ifdef DEBUG
	DebugSerial.println(F("basics wrote"));
#endif

	//copy card content to flash. все страницы не на чинающиеся с 0
	pageFlash += 16;
	uint8_t checkCount = 0;
	for (uint8_t page = 0; page < TAG_MAX_PAGE - 3; page = page + 4)
	{
#ifdef DEBUG
		DebugSerial.print(F("reading page: "));
		DebugSerial.println(String(page));
#endif
		if (!ntagRead4pages(page))
		{
			return false;
		}
		if (page < 8 || ntag_page[0]>0)
		{
			SPIflash.writeByteArray(pageFlash + (uint32_t)page * (uint32_t)4, ntag_page, 16);
			checkCount++;
#ifdef DEBUG
			DebugSerial.print(F("write: "));
			for (uint8_t i = 0; i < 16; i++)DebugSerial.print(String(ntag_page[i], HEX) + " ");
			DebugSerial.println();
#endif
		}
		if (ntag_page[0] == 0)
		{
#ifdef DEBUG
			DebugSerial.print(F("chip end: "));
			DebugSerial.println(String(page));
#endif
			break;
		}
		}
	//add dump pages number
	SPIflash.writeByte((uint32_t)((uint32_t)recordNum * (uint32_t)LOG_RECORD_LENGTH + (uint32_t)12), checkCount);
	return true;
		}

//получаем сведения о команде из лога
bool readTeamFromFlash(uint16_t recordNum)
{
	uint32_t addr = (uint32_t)((uint32_t)recordNum * (uint32_t)LOG_RECORD_LENGTH);
	if (SPIflash.readByte(addr) == 255) return false;
	//#команды
	//время инициализации
	//маска
	//время отметки
	for (uint8_t i = 0; i < 13; i++)
	{
		ntag_page[i] = SPIflash.readByte(addr + (uint32_t)i);
	}
	return true;
}

//подсчет записанных в флэш отметок
uint16_t refreshChipCounter()
{
	uint16_t chips = 0;

	uint32_t addr;
	for (uint16_t i = 1; i < LOG_LENGTH; i++)
	{
		addr = (uint32_t)((uint32_t)i * (uint32_t)LOG_RECORD_LENGTH);
		if (SPIflash.readByte(addr) != 255)
		{
			chips++;
			uint32_t time = SPIflash.readByte(addr + 8);
			time <<= 8;
			time += SPIflash.readByte(addr + 9);
			time <<= 8;
			time += SPIflash.readByte(addr + 10);
			time <<= 8;
			time += SPIflash.readByte(addr + 11);
			if (time > lastTimeChecked)
			{
				lastTimeChecked = time;
				lastTeams[0] = SPIflash.readByte(addr);
				lastTeams[1] = SPIflash.readByte(addr + 1);
			}
#ifdef DEBUG			
			DebugSerial.println(String(i));
#endif
		}
	}
#ifdef DEBUG
	DebugSerial.print(F("chip counter="));
	DebugSerial.println(String(chips));
#endif
	return chips;
}

//обработка ошибок. формирование пакета с сообщением о ошибке
void sendError(uint8_t errorCode, uint8_t commandCode)
{
	init_package(commandCode);
	uartBuffer[DATA_START_BYTE] = errorCode;
	uartBufferPosition = DATA_START_BYTE + 1;
	sendData();
	}

uint8_t crcCalc(uint8_t* dataArray, uint16_t startPosition, uint16_t dataEnd)
{
	uint8_t crc = 0x00;
	uint16_t i = startPosition;
	while (i <= dataEnd)
	{
		uint8_t tmpByte = dataArray[i];
		for (uint8_t tempI = 8; tempI; tempI--)
		{
			uint8_t sum = (crc ^ tmpByte) & 0x01;
			crc >>= 1;
			if (sum)
			{
				crc ^= 0x8C;
			}
			tmpByte >>= 1;
		}
		i++;
	}
	return (crc);
}

void addLastTeam(uint16_t number)
{
	for (uint8_t i = lastTeamsLength * 2 - 1; i > 1; i = i - 2)
	{
		lastTeams[i] = lastTeams[i - 2];
		lastTeams[i - 1] = lastTeams[i - 3];
	}
	lastTeams[0] = (byte)(number >> 8);
	lastTeams[1] = (byte)number;
}

void floatToByte(byte* bytes, float f)
{
	int length = sizeof(float);

	for (int i = 0; i < length; i++)
	{
		bytes[i] = ((byte*)&f)[i];
	}
}
