/******************************************************************************
 *
 *  Copyright (C) 2002-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  This file contains the HID HOST API entry points
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bt_common.h"
#include "bt_types.h"
#include "btm_api.h"
#include "btm_int.h"
#include "btu.h"
#include "hiddefs.h"
#include "hidh_api.h"
#include "hidh_int.h"

using bluetooth::Uuid;

#define HID_USAGE_PAGE             0x05
#define HID_USAGE_PAGE_ID_SENSOR   0x20
#define HID_USAGE_SENSOR_TYPE      0x09
#define HID_USAGE_SENSOR_TYPE_COLL 0x01

tHID_HOST_CTB hh_cb;

static void hidh_search_callback(uint16_t sdp_result);

/*******************************************************************************
 *
 * Function         HID_HostGetSDPRecord
 *
 * Description      This function reads the device SDP record
 *
 * Returns          tHID_STATUS
 *
 ******************************************************************************/
tHID_STATUS HID_HostGetSDPRecord(const RawAddress& addr,
                                 tSDP_DISCOVERY_DB* p_db, uint32_t db_len,
                                 tHID_HOST_SDP_CALLBACK* sdp_cback) {

  if (hh_cb.sdp_busy) return HID_ERR_SDP_BUSY;

  hh_cb.p_sdp_db = p_db;
  Uuid uuid_list = Uuid::From16Bit(UUID_SERVCLASS_HUMAN_INTERFACE);
  SDP_InitDiscoveryDb(p_db, db_len, 1, &uuid_list, 0, NULL);

  if (SDP_ServiceSearchRequest(addr, p_db, hidh_search_callback)) {
    hh_cb.sdp_cback = sdp_cback;
    hh_cb.sdp_busy = true;
    return HID_SUCCESS;
  } else
    return HID_ERR_NO_RESOURCES;
}

void hidh_get_str_attr(tSDP_DISC_REC* p_rec, uint16_t attr_id, uint16_t max_len,
                       char* str) {
  tSDP_DISC_ATTR* p_attr;
  uint16_t name_len;

  p_attr = SDP_FindAttributeInRec(p_rec, attr_id);
  if (p_attr != NULL) {
    uint8_t arr_len = sizeof(p_attr->attr_value.v.array) /
                      sizeof(p_attr->attr_value.v.array[0]);
    name_len = SDP_DISC_ATTR_LEN(p_attr->attr_len_type);
    if (name_len < max_len && name_len < (arr_len - 1)) {
      memcpy(str, (char*)p_attr->attr_value.v.array, name_len);
      str[name_len] = '\0';
    } else if (max_len < arr_len) {
      memcpy(str, (char*)p_attr->attr_value.v.array, max_len - 1);
      str[max_len - 1] = '\0';
    }
  } else
    str[0] = '\0';
}

static void hidh_search_callback(uint16_t sdp_result) {
  tSDP_DISCOVERY_DB* p_db = hh_cb.p_sdp_db;
  tSDP_DISC_REC* p_rec;
  tSDP_DISC_ATTR *p_attr, *p_subattr1, *p_subattr2, *p_repdesc;
  tHID_DEV_SDP_INFO* p_nvi = &hh_cb.sdp_rec;
  uint16_t attr_mask = 0;


  hh_cb.sdp_busy = false;

  if (sdp_result != SDP_SUCCESS) {
    hh_cb.sdp_cback(sdp_result, 0, NULL);
    return;
  }

  Uuid hid_uuid = Uuid::From16Bit(UUID_SERVCLASS_HUMAN_INTERFACE);
  p_rec = SDP_FindServiceUUIDInDb(p_db, hid_uuid, NULL);
  if (p_rec == NULL) {
    hh_cb.sdp_cback(HID_SDP_NO_SERV_UUID, 0, NULL);
    return;
  }

  memset(&hh_cb.sdp_rec, 0, sizeof(tHID_DEV_SDP_INFO));

  /* First, verify the mandatory fields we care about */
  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_DESCRIPTOR_LIST)) ==
       NULL) ||
      (SDP_DISC_ATTR_TYPE(p_attr->attr_len_type) != DATA_ELE_SEQ_DESC_TYPE) ||
      ((p_subattr1 = p_attr->attr_value.v.p_sub_attr) == NULL) ||
      (SDP_DISC_ATTR_TYPE(p_subattr1->attr_len_type) !=
       DATA_ELE_SEQ_DESC_TYPE) ||
      ((p_subattr2 = p_subattr1->attr_value.v.p_sub_attr) == NULL) ||
      ((p_repdesc = p_subattr2->p_next_attr) == NULL) ||
      (SDP_DISC_ATTR_TYPE(p_repdesc->attr_len_type) != TEXT_STR_DESC_TYPE)) {
    hh_cb.sdp_cback(HID_SDP_MANDATORY_MISSING, 0, NULL);
    return;
  }

  p_nvi->dscp_info.dl_len = SDP_DISC_ATTR_LEN(p_repdesc->attr_len_type);
  if (p_nvi->dscp_info.dl_len != 0)
    p_nvi->dscp_info.dsc_list = (uint8_t*)&p_repdesc->attr_value;

  // TODO: Proper check needed
  if ((p_nvi->dscp_info.dl_len > 3) && (p_nvi->dscp_info.dsc_list != NULL) &&
         (p_nvi->dscp_info.dsc_list[0] == HID_USAGE_PAGE) &&
         (p_nvi->dscp_info.dsc_list[1] == HID_USAGE_PAGE_ID_SENSOR) &&
         (p_nvi->dscp_info.dsc_list[2] == HID_USAGE_SENSOR_TYPE) &&
         (p_nvi->dscp_info.dsc_list[3] == HID_USAGE_SENSOR_TYPE_COLL)) {
    attr_mask |= HID_3D_AUDIO;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_VIRTUAL_CABLE)) !=
       NULL) &&
      (p_attr->attr_value.v.u8)) {
    attr_mask |= HID_VIRTUAL_CABLE;
  }

  if (((p_attr = SDP_FindAttributeInRec(
            p_rec, ATTR_ID_HID_RECONNECT_INITIATE)) != NULL) &&
      (p_attr->attr_value.v.u8)) {
    attr_mask |= HID_RECONN_INIT;
  }

  if (((p_attr = SDP_FindAttributeInRec(
            p_rec, ATTR_ID_HID_NORMALLY_CONNECTABLE)) != NULL) &&
      (p_attr->attr_value.v.u8)) {
    attr_mask |= HID_NORMALLY_CONNECTABLE;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_SDP_DISABLE)) !=
       NULL) &&
      (p_attr->attr_value.v.u8)) {
    attr_mask |= HID_SDP_DISABLE;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_BATTERY_POWER)) !=
       NULL) &&
      (p_attr->attr_value.v.u8)) {
    attr_mask |= HID_BATTERY_POWER;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_REMOTE_WAKE)) !=
       NULL) &&
      (p_attr->attr_value.v.u8)) {
    attr_mask |= HID_REMOTE_WAKE;
  }

  hidh_get_str_attr(p_rec, ATTR_ID_SERVICE_NAME, HID_MAX_SVC_NAME_LEN,
                    p_nvi->svc_name);
  hidh_get_str_attr(p_rec, ATTR_ID_SERVICE_DESCRIPTION, HID_MAX_SVC_DESCR_LEN,
                    p_nvi->svc_descr);
  hidh_get_str_attr(p_rec, ATTR_ID_PROVIDER_NAME, HID_MAX_PROV_NAME_LEN,
                    p_nvi->prov_name);

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_DEVICE_RELNUM)) !=
       NULL)) {
    p_nvi->rel_num = p_attr->attr_value.v.u16;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_COUNTRY_CODE)) !=
       NULL)) {
    p_nvi->ctry_code = p_attr->attr_value.v.u8;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_DEVICE_SUBCLASS)) !=
       NULL)) {
    p_nvi->sub_class = p_attr->attr_value.v.u8;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_PARSER_VERSION)) !=
       NULL)) {
    p_nvi->hpars_ver = p_attr->attr_value.v.u16;
  }

  if (((p_attr = SDP_FindAttributeInRec(
            p_rec, ATTR_ID_HID_LINK_SUPERVISION_TO)) != NULL)) {
    attr_mask |= HID_SUP_TOUT_AVLBL;
    p_nvi->sup_timeout = p_attr->attr_value.v.u16;
  }

  if (((p_attr = SDP_FindAttributeInRec(p_rec, ATTR_ID_HID_SSR_HOST_MAX_LAT)) !=
       NULL)) {
    attr_mask |= HID_SSR_MAX_LATENCY;
    p_nvi->ssr_max_latency = p_attr->attr_value.v.u16;
  } else
    p_nvi->ssr_max_latency = HID_SSR_PARAM_INVALID;

  if (((p_attr = SDP_FindAttributeInRec(
            p_rec, ATTR_ID_HID_SSR_HOST_MIN_TOUT)) != NULL)) {
    attr_mask |= HID_SSR_MIN_TOUT;
    p_nvi->ssr_min_tout = p_attr->attr_value.v.u16;
  } else
    p_nvi->ssr_min_tout = HID_SSR_PARAM_INVALID;

  hh_cb.sdp_rec.p_sdp_layer_rec = p_rec;
  hh_cb.sdp_cback(SDP_SUCCESS, attr_mask, &hh_cb.sdp_rec);
}

/*******************************************************************************
 *
 * Function         HID_HostInit
 *
 * Description      This function initializes the control block and trace
 *                  variable
 *
 * Returns          void
 *
 ******************************************************************************/
void HID_HostInit(void) {
  uint8_t log_level = hh_cb.trace_level;
  memset(&hh_cb, 0, sizeof(tHID_HOST_CTB));
  hh_cb.trace_level = log_level;
}

/*******************************************************************************
 *
 * Function         HID_HostSetTraceLevel
 *
 * Description      This function sets the trace level for HID Host. If called
 *                  with 0xFF, it simply reads the current trace level.
 *
 * Returns          the new (current) trace level
 *
 ******************************************************************************/
uint8_t HID_HostSetTraceLevel(uint8_t new_level) {
  if (new_level != 0xFF) hh_cb.trace_level = new_level;

  return (hh_cb.trace_level);
}

/*******************************************************************************
 *
 * Function         HID_HostRegister
 *
 * Description      This function registers HID-Host with lower layers
 *
 * Returns          tHID_STATUS
 *
 ******************************************************************************/
tHID_STATUS HID_HostRegister(tHID_HOST_DEV_CALLBACK* dev_cback) {
  tHID_STATUS st;

  if (hh_cb.reg_flag) return HID_ERR_ALREADY_REGISTERED;

  if (dev_cback == NULL) return HID_ERR_INVALID_PARAM;

  /* Register with L2CAP */
  st = hidh_conn_reg();
  if (st != HID_SUCCESS) {
    return st;
  }

  hh_cb.callback = dev_cback;
  hh_cb.reg_flag = true;

  for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
    hh_cb.devices[i].conn.process_repage_timer =
        alarm_new("hid_devices_conn.process_repage_timer");
  }
  return (HID_SUCCESS);
}

/*******************************************************************************
 *
 * Function         HID_HostDeregister
 *
 * Description      This function is called when the host is about power down.
 *
 * Returns          tHID_STATUS
 *
 ******************************************************************************/
tHID_STATUS HID_HostDeregister(void) {
  uint8_t i;

  if (!hh_cb.reg_flag) return (HID_ERR_NOT_REGISTERED);

  for (i = 0; i < HID_HOST_MAX_DEVICES; i++) {
    HID_HostRemoveDev(i);
    alarm_free(hh_cb.devices[i].conn.process_repage_timer);
  }

  hidh_conn_dereg();
  hh_cb.reg_flag = false;

  return (HID_SUCCESS);
}

/*******************************************************************************
 *
 * Function         HID_HostAddDev
 *
 * Description      This is called so HID-host may manage this device.
 *
 * Returns          tHID_STATUS
 *
 ******************************************************************************/
tHID_STATUS HID_HostAddDev(const RawAddress& addr, uint16_t attr_mask,
                           uint8_t* handle) {
  int i;
  /* Find an entry for this device in hh_cb.devices array */
  if (!hh_cb.reg_flag) return (HID_ERR_NOT_REGISTERED);

  for (i = 0; i < HID_HOST_MAX_DEVICES; i++) {
    if ((hh_cb.devices[i].in_use) && addr == hh_cb.devices[i].addr) break;
  }

  if (i == HID_HOST_MAX_DEVICES) {
    for (i = 0; i < HID_HOST_MAX_DEVICES; i++) {
      if (!hh_cb.devices[i].in_use) break;
    }
  }

  if (i == HID_HOST_MAX_DEVICES) {
    HIDH_TRACE_ERROR(" HID_ERR_NO_RESOURCES ");
    return HID_ERR_NO_RESOURCES;
  }

  if (!hh_cb.devices[i].in_use) {
    hh_cb.devices[i].in_use = true;
    hh_cb.devices[i].addr = addr;
    hh_cb.devices[i].state = HID_DEV_NO_CONN;
    hh_cb.devices[i].conn_tries = 0;
  }

  if (attr_mask != HID_ATTR_MASK_IGNORE) hh_cb.devices[i].attr_mask = attr_mask;

  *handle = i;

  return (HID_SUCCESS);
}

/*******************************************************************************
 *
 * Function         HID_HostRemoveDev
 *
 * Description      This removes the device from the list of devices that the
 *                  host has to manage.
 *
 * Returns          tHID_STATUS
 *
 ******************************************************************************/
tHID_STATUS HID_HostRemoveDev(uint8_t dev_handle) {
  if (!hh_cb.reg_flag) return (HID_ERR_NOT_REGISTERED);

  if ((dev_handle >= HID_HOST_MAX_DEVICES) ||
      (!hh_cb.devices[dev_handle].in_use))
    return HID_ERR_INVALID_PARAM;

  HID_HostCloseDev(dev_handle);
  hh_cb.devices[dev_handle].in_use = false;
  hh_cb.devices[dev_handle].conn.conn_state = HID_CONN_STATE_UNUSED;
  hh_cb.devices[dev_handle].conn.ctrl_cid =
      hh_cb.devices[dev_handle].conn.intr_cid = 0;
  hh_cb.devices[dev_handle].attr_mask = 0;
  return HID_SUCCESS;
}

/*******************************************************************************
 *
 * Function         HID_HostOpenDev
 *
 * Description      This function is called when the user wants to initiate a
 *                  connection attempt to a device.
 *
 * Returns          void
 *
 ******************************************************************************/
tHID_STATUS HID_HostOpenDev(uint8_t dev_handle) {
  if (!hh_cb.reg_flag) return (HID_ERR_NOT_REGISTERED);

  if ((dev_handle >= HID_HOST_MAX_DEVICES) ||
      (!hh_cb.devices[dev_handle].in_use))
    return HID_ERR_INVALID_PARAM;

  if (hh_cb.devices[dev_handle].state != HID_DEV_NO_CONN)
    return HID_ERR_ALREADY_CONN;

  hh_cb.devices[dev_handle].conn_tries = 1;
  return hidh_conn_initiate(dev_handle);
}

/*******************************************************************************
 *
 * Function         HID_HostWriteDev
 *
 * Description      This function is called when the host has a report to send.
 *
 *                  report_id: is only used on GET_REPORT transaction if is
 *                              specified. only valid when it is non-zero.
 *
 * Returns          void
 *
 ******************************************************************************/
tHID_STATUS HID_HostWriteDev(uint8_t dev_handle, uint8_t t_type, uint8_t param,
                             uint16_t data, uint8_t report_id, BT_HDR* pbuf) {
  tHID_STATUS status = HID_SUCCESS;

  if (!hh_cb.reg_flag) {
    HIDH_TRACE_ERROR("HID_ERR_NOT_REGISTERED");
    status = HID_ERR_NOT_REGISTERED;
  }

  if ((dev_handle >= HID_HOST_MAX_DEVICES) ||
      (!hh_cb.devices[dev_handle].in_use)) {
    HIDH_TRACE_ERROR("HID_ERR_INVALID_PARAM");
    status = HID_ERR_INVALID_PARAM;
  }

  else if (hh_cb.devices[dev_handle].state != HID_DEV_CONNECTED) {
    HIDH_TRACE_ERROR("HID_ERR_NO_CONNECTION dev_handle %d", dev_handle);
    status = HID_ERR_NO_CONNECTION;
  }

  if (status != HID_SUCCESS)
    osi_free(pbuf);
  else
    status =
        hidh_conn_snd_data(dev_handle, t_type, param, data, report_id, pbuf);

  return status;
}

/*******************************************************************************
 *
 * Function         HID_HostCloseDev
 *
 * Description      This function disconnects the device.
 *
 * Returns          void
 *
 ******************************************************************************/
tHID_STATUS HID_HostCloseDev(uint8_t dev_handle) {
  if (!hh_cb.reg_flag) return (HID_ERR_NOT_REGISTERED);

  if ((dev_handle >= HID_HOST_MAX_DEVICES) ||
      (!hh_cb.devices[dev_handle].in_use))
    return HID_ERR_INVALID_PARAM;

  if (hh_cb.devices[dev_handle].state != HID_DEV_CONNECTED)
    return HID_ERR_NO_CONNECTION;

  alarm_cancel(hh_cb.devices[dev_handle].conn.process_repage_timer);
  hh_cb.devices[dev_handle].conn_tries = HID_HOST_MAX_CONN_RETRY + 1;
  return hidh_conn_disconnect(dev_handle);
}

tHID_STATUS HID_HostSetSecurityLevel(const char serv_name[], uint8_t sec_lvl) {
  if (!BTM_SetSecurityLevel(false, serv_name, BTM_SEC_SERVICE_HIDH_SEC_CTRL,
                            sec_lvl, HID_PSM_CONTROL, BTM_SEC_PROTO_HID,
                            HID_SEC_CHN)) {
    HIDH_TRACE_ERROR("Security Registration 1 failed");
    return (HID_ERR_NO_RESOURCES);
  }

  if (!BTM_SetSecurityLevel(true, serv_name, BTM_SEC_SERVICE_HIDH_SEC_CTRL,
                            sec_lvl, HID_PSM_CONTROL, BTM_SEC_PROTO_HID,
                            HID_SEC_CHN)) {
    HIDH_TRACE_ERROR("Security Registration 2 failed");
    return (HID_ERR_NO_RESOURCES);
  }

  if (!BTM_SetSecurityLevel(false, serv_name, BTM_SEC_SERVICE_HIDH_NOSEC_CTRL,
                            BTM_SEC_NONE, HID_PSM_CONTROL, BTM_SEC_PROTO_HID,
                            HID_NOSEC_CHN)) {
    HIDH_TRACE_ERROR("Security Registration 3 failed");
    return (HID_ERR_NO_RESOURCES);
  }

  if (!BTM_SetSecurityLevel(true, serv_name, BTM_SEC_SERVICE_HIDH_NOSEC_CTRL,
                            BTM_SEC_NONE, HID_PSM_CONTROL, BTM_SEC_PROTO_HID,
                            HID_NOSEC_CHN)) {
    HIDH_TRACE_ERROR("Security Registration 4 failed");
    return (HID_ERR_NO_RESOURCES);
  }

  if (!BTM_SetSecurityLevel(true, serv_name, BTM_SEC_SERVICE_HIDH_INTR,
                            BTM_SEC_NONE, HID_PSM_INTERRUPT, BTM_SEC_PROTO_HID,
                            0)) {
    HIDH_TRACE_ERROR("Security Registration 5 failed");
    return (HID_ERR_NO_RESOURCES);
  }

  if (!BTM_SetSecurityLevel(false, serv_name, BTM_SEC_SERVICE_HIDH_INTR,
                            BTM_SEC_NONE, HID_PSM_INTERRUPT, BTM_SEC_PROTO_HID,
                            0)) {
    HIDH_TRACE_ERROR("Security Registration 6 failed");
    return (HID_ERR_NO_RESOURCES);
  }

  return (HID_SUCCESS);
}

/******************************************************************************
 *
 * Function         hid_known_hid_device
 *
 * Description      check if this device is  of type HID Device
 *
 * Returns          true if device is HID Device else false
 *
 ******************************************************************************/
bool hid_known_hid_device(const RawAddress& bd_addr) {
  uint8_t i;
  tBTM_INQ_INFO* p_inq_info = BTM_InqDbRead(bd_addr);

  if (!hh_cb.reg_flag) return false;

  /* First  check for class of device , if Inq DB has information about this
   * device*/
  if (p_inq_info != NULL) {
    /* Check if remote major device class is of type BTM_COD_MAJOR_PERIPHERAL */
    if ((p_inq_info->results.dev_class[1] & BTM_COD_MAJOR_CLASS_MASK) ==
        BTM_COD_MAJOR_PERIPHERAL) {
      HIDH_TRACE_DEBUG(
          "hid_known_hid_device:dev found in InqDB & COD matches HID dev");
      return true;
    }
  } else {
    /* Look for this device in security device DB */
    tBTM_SEC_DEV_REC* p_dev_rec = btm_find_dev(bd_addr);
    if ((p_dev_rec != NULL) &&
        ((p_dev_rec->dev_class[1] & BTM_COD_MAJOR_CLASS_MASK) ==
         BTM_COD_MAJOR_PERIPHERAL)) {
      HIDH_TRACE_DEBUG(
          "hid_known_hid_device:dev found in SecDevDB & COD matches HID dev");
      return true;
    }
  }

  /* Find an entry for this device in hh_cb.devices array */
  for (i = 0; i < HID_HOST_MAX_DEVICES; i++) {
    if ((hh_cb.devices[i].in_use) && bd_addr == hh_cb.devices[i].addr)
      return true;
  }
  /* Check if this device is marked as HID Device in IOP Dev */
  HIDH_TRACE_DEBUG("hid_known_hid_device:remote is not HID device");
  return false;
}
