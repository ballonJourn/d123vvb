#ifndef __FSCBT_INTERFACE_H__
#define __FSCBT_INTERFACE_H__

#include <stdint.h>

#define AT_CMD_TX_QUEUE_LEN 20
#define AT_CMD_RX_QUEUE_LEN 100
#define AT_CMD_PAYLOAD_LEN 4096

#define DAC_BUFFER_SIZE (64*1024)
#define ADC_BUFFER_SIZE (64*1024)

typedef enum
{
	BLE_EASY_CONNECTION = 0,/* for EASYCONNECTION */
	BLE_ERYA_CONNECTION = 1,/* for ERYA CONNECTION */
	BLE_DEFAULT_CONNECTION = 2,/* for DEFAULT CONNECTION*/
	BLE_DEFAULT1_CONNECTION = 3,/* fOr DEFAULT1 CONNECTION */
	BLE_CONNECTION_END
}BLE_CONNECTION_TYPE_E;

typedef enum
{
	FSC_MODULE_BW121 = 0,/*BW121/BW120*/
	FSC_M0DULE_BW110 = 1, /*8BW110k*/
	FSC_MODULE_BT805 = 2,
	FSC_MODULE_END
}MODULE_TYPE_E;

typedef enum
{
	AUD_PROCESS_SCO = 0,/*sco*/
	AUD_PROCESS_PLAYBACK = 1,/* playback*/
	AUD_PROCESS_PLAYBACKRES = 2,/* playbackres*/
	AUD_PROCESS_RINGTONE = 3,/* ringtone*/
	AUD_PROCESS_END
}AUD_PROCESS_MODE_E;

typedef struct
{
	int uart_type;
	const char* uart_dev;
	int uart_port;
	unsigned int vart_tx_pin;
	unsigned int uart_rx_pin;
	unsigned int bt_en_pin;
}bt_hw_cfg_t;

typedef struct
{
	void* tx_queue;					/* AT-Command sent from upper layer application to blueware */
	void* rx_queue;					/* AT-Response sent from blueware to upper layer application */
	int a2dp_resampler;				/* not support.default 0 */
	int debug_mode;					/* turn blueware debug log on/off */
	unsigned char ble_connection_type;	/* BLE_CONNECTION_TYPE_E */
	unsigned char ancs_enable;			/* ancs on/off */
    unsigned char absvol_enable;		/*k abs vol on/off */
	int module_type;     //M00ULE_TYPE_E
}bt_sw_cfg_t;

/*!
	\brief: initialise bluetooth stack (blueware)
	\param hw_cfg: hardware configuration
	\param sw_cfg: software configuration
*/
int fscbt_init(const bt_hw_cfg_t *hw_cfg,const bt_sw_cfg_t *sw_cfg);

/*
	\brief: blueware main loop, running in a standalone task
*/
void fscbt_run(void);

/*!
	\brief: wake up blueooth task from sleeping
	\param from_isr: set to 1 if call from isr, othewise set to 0
*/
void fscbt_wakeup(int from_isr);

/*!
	\brief:bluewarereaddatabase
*/
int bt_nvm_read(unsigned char* data, unsigned short len, unsigned char page);

/*!
	\brief: blueware write database
*/
int bt_nvm_write(unsigned char* data, unsigned short len, unsigned char page);

/*!
	brief:gocStack close database
*/
int bt_nvm_close(void * fp,const char* path);

/*!
	\brief: get avrcp coverart image data..
	
	\param * pu32len: Image Data Length.
		0:No Data.
	\return:
		JPG Image Data Address:
		start data:0xff,0xd8
		end data:0xff,0xd9
*/	
void fscbt_get_coverart_data(char** pImage,unsigned int* pu32len);

/*!
	\brief:i2s DAC init
*/
void bt_aud_i2s_dac_init(int mode, int samplerate, unsigned char*dac_buf);

/*!
	\brief:i2s DAC deinit
*/
void bt_aud_i2s_dac_deinit(unsigned char*dac_buf);

/*!
	\brief:i2s ADC init
*/
void bt_aud_i2s_adc_init(int mode, int samplerate, unsigned char*adc_buf);

/*!
	\brief:i2s ADC deinit
*/
void bt_aud_i2s_adc_deinit(unsigned char*adc_buf);

int GocsdkStartUp();

#endif