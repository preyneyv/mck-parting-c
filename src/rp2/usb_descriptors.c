#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#define USBD_VID 0x2E8A
#define USBD_PID 0x000A

#define WEBUSB_VENDOR_REQUEST 0x01
#define MICROSOFT_VENDOR_REQUEST 0x02

enum
{
  ITF_CDC = 0,
  ITF_CDC_DATA,
  ITF_MIDI,
  ITF_MIDI_STREAMING,
  ITF_MANAGEMENT,
  ITF_COUNT,
};

enum
{
  STR_LANG = 0,
  STR_MANUFACTURER,
  STR_PRODUCT,
  STR_SERIAL,
  STR_CDC,
  STR_MIDI,
  STR_MANAGEMENT,
};

#define EP_CDC_NOTIFY 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_MIDI_OUT 0x03
#define EP_MIDI_IN 0x83
#define EP_MANAGEMENT_OUT 0x04
#define EP_MANAGEMENT_IN 0x84
#define EP_SIZE 64

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    .bcdDevice = 0x0200,
    .iManufacturer = STR_MANUFACTURER,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

#define CONFIG_LEN                                                           \
  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN +             \
   TUD_VENDOR_DESC_LEN)

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 500),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIFY, 8, EP_CDC_OUT,
                       EP_CDC_IN, EP_SIZE),
    TUD_MIDI_DESCRIPTOR(ITF_MIDI, STR_MIDI, EP_MIDI_OUT, EP_MIDI_IN, EP_SIZE),
    TUD_VENDOR_DESCRIPTOR(ITF_MANAGEMENT, STR_MANAGEMENT, EP_MANAGEMENT_OUT,
                          EP_MANAGEMENT_IN, EP_SIZE),
};

#define MS_OS_20_DESC_LEN 0xB2
#define BOS_LEN                                                              \
  (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN +                             \
   TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t bos_descriptor[] = {
    TUD_BOS_DESCRIPTOR(BOS_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(WEBUSB_VENDOR_REQUEST, 0),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN,
                                MICROSOFT_VENDOR_REQUEST),
};

/* Binds only the management interface to WinUSB. The stable interface GUID is
 * intentionally device-family-wide; the USB serial distinguishes units. */
static const uint8_t ms_os_20_descriptor[] = {
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_MANAGEMENT, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY), U16_TO_U8S_LE(0x0007),
    U16_TO_U8S_LE(0x002A),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0,
    't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0,
    'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(0x0050),
    '{', 0, '8', 0, '7', 0, '6', 0, '2', 0, '0', 0, '0', 0, 'B', 0,
    'E', 0, '-', 0, '8', 0, '7', 0, '0', 0, '0', 0, '-', 0, '4', 0,
    'C', 0, 'D', 0, 'B', 0, '-', 0, 'A', 0, '9', 0, '7', 0, '1', 0,
    '-', 0, 'D', 0, '7', 0, 'E', 0, '2', 0, '5', 0, 'D', 0, 'E', 0,
    'B', 0, '4', 0, '0', 0, 'D', 0, '6', 0, '}', 0, 0, 0, 0, 0,
};

TU_VERIFY_STATIC(sizeof(ms_os_20_descriptor) == MS_OS_20_DESC_LEN,
                 "invalid Microsoft OS descriptor length");

static char serial_string[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
static const char *const strings[] = {
    [STR_MANUFACTURER] = "preyneyv",
    [STR_PRODUCT] = "prism",
    [STR_SERIAL] = serial_string,
    [STR_CDC] = "prism debug",
    [STR_MIDI] = "prism MIDI",
    [STR_MANAGEMENT] = "prism management",
};

const uint8_t *tud_descriptor_device_cb(void)
{
  return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return configuration_descriptor;
}

const uint8_t *tud_descriptor_bos_cb(void) { return bos_descriptor; }

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void)langid;
  static uint16_t descriptor[32];
  uint8_t len = 0;

  if (index == STR_LANG)
  {
    descriptor[1] = 0x0409;
    len = 1;
  }
  else
  {
    if (index >= sizeof(strings) / sizeof(strings[0]))
      return NULL;
    if (index == STR_SERIAL && serial_string[0] == '\0')
      pico_get_unique_board_id_string(serial_string, sizeof(serial_string));
    const char *value = strings[index];
    if (value == NULL)
      return NULL;
    while (value[len] != '\0' && len < 31)
    {
      descriptor[len + 1] = value[len];
      ++len;
    }
  }
  descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (len * 2 + 2));
  return descriptor;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request)
{
  if (stage != CONTROL_STAGE_SETUP)
    return true;

  if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
      request->bRequest == MICROSOFT_VENDOR_REQUEST &&
      request->wIndex == 7)
  {
    return tud_control_xfer(rhport, request, (void *)ms_os_20_descriptor,
                            sizeof(ms_os_20_descriptor));
  }
  return false;
}
