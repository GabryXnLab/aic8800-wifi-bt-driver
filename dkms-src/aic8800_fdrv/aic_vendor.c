#include "aic_vendor.h"
#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <net/netlink.h>
#include "rwnx_version_gen.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)

static struct wifi_ring_buffer_status ring_buffer[] = {
	{
		.name            = "aicwf_ring_buffer0",
		.flags           = 0,
		.ring_id         = 0,
		.verbose_level   = 0,
		.written_bytes   = 0,
		.read_bytes      = 0,
		.written_records = 0,
	},
};

static struct wlan_driver_wake_reason_cnt_t wake_reason_cnt = {
	.total_cmd_event_wake = 10,
};

enum apf_attributes {
        APF_ATTRIBUTE_VERSION,
        APF_ATTRIBUTE_MAX_LEN,
        APF_ATTRIBUTE_PROGRAM,
        APF_ATTRIBUTE_PROGRAM_LEN,
        APF_ATTRIBUTE_MAX
};

#endif

int aic_dev_start_mkeep_alive(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			u8 mkeep_alive_id, u8 *ip_pkt, u16 ip_pkt_len, u8 *src_mac, u8 *dst_mac, u32 period_msec)
{
	u8 *data, *pos;

	data = kzalloc(ip_pkt_len + 14, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pos = data;
	memcpy(pos, dst_mac, 6);
	pos += 6;
	memcpy(pos, src_mac, 6);
	pos += 6;
	/* Mapping Ethernet type (ETHERTYPE_IP: 0x0800) */
	*(pos++) = 0x08;
	*(pos++) = 0x00;

	/* Mapping IP pkt */
	memcpy(pos, ip_pkt, ip_pkt_len);
	pos += ip_pkt_len;

	//add send 802.3 pkt(raw data)
	kfree(data);

	return 0;
}

int aic_dev_stop_mkeep_alive(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u8 mkeep_alive_id)
{
	int  res = -1;

	/*
	 * The mkeep_alive packet is for STA interface only; if the bss is configured as AP,
	 * dongle shall reject a mkeep_alive request.
	 */
	if (rwnx_vif->wdev.iftype != NL80211_IFTYPE_STATION)
		return res;

	printk("%s execution\n", __func__);

	//add send stop keep alive
	res = 0;
	return res;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
static int aicwf_vendor_start_mkeep_alive(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	/* max size of IP packet for keep alive */
	const int MKEEP_ALIVE_IP_PKT_MAX = 256;

	int ret = 0, rem, type;
	u8 mkeep_alive_id = 0;
	u8 *ip_pkt = NULL;
	u16 ip_pkt_len = 0;
	u8 src_mac[6];
	u8 dst_mac[6];
	u32 period_msec = 0;
	const struct nlattr *iter;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
	gfp_t kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	printk("%s\n", __func__);

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case MKEEP_ALIVE_ATTRIBUTE_ID:
			mkeep_alive_id = nla_get_u8(iter);
			break;
		case MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN:
			ip_pkt_len = nla_get_u16(iter);
			if (ip_pkt_len > MKEEP_ALIVE_IP_PKT_MAX) {
				ret = -EINVAL;
				goto exit;
			}
			break;
		case MKEEP_ALIVE_ATTRIBUTE_IP_PKT:
			if (!ip_pkt_len) {
				ret = -EINVAL;
				printk("ip packet length is 0\n");
				goto exit;
			}
			ip_pkt = (u8 *)kzalloc(ip_pkt_len, kflags);
			if (ip_pkt == NULL) {
				ret = -ENOMEM;
				printk("Failed to allocate mem for ip packet\n");
				goto exit;
			}
			memcpy(ip_pkt, (u8 *)nla_data(iter), ip_pkt_len);
			break;
		case MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR:
			memcpy(src_mac, nla_data(iter), 6);
			break;
		case MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR:
			memcpy(dst_mac, nla_data(iter), 6);
			break;
		case MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC:
			period_msec = nla_get_u32(iter);
			break;
		default:
			pr_err("%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			ret = -EINVAL;
			goto exit;
		}
	}

	if (ip_pkt == NULL) {
		ret = -EINVAL;
		printk("ip packet is NULL\n");
		goto exit;
	}

	ret = aic_dev_start_mkeep_alive(rwnx_hw, rwnx_vif, mkeep_alive_id, ip_pkt, ip_pkt_len, src_mac,
		dst_mac, period_msec);
	if (ret < 0) {
		printk("start_mkeep_alive is failed ret: %d\n", ret);
	}

exit:
	if (ip_pkt) {
		kfree(ip_pkt);
	}

	return ret;
}

static int aicwf_vendor_stop_mkeep_alive(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type;
	u8 mkeep_alive_id = 0;
	const struct nlattr *iter;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);

	printk("%s\n", __func__);
	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case MKEEP_ALIVE_ATTRIBUTE_ID:
			mkeep_alive_id = nla_get_u8(iter);
			break;
		default:
			pr_err("%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			ret = -EINVAL;
			break;
		}
	}

	ret = aic_dev_stop_mkeep_alive(rwnx_hw, rwnx_vif, mkeep_alive_id);
	if (ret < 0) {
		printk("stop_mkeep_alive is failed ret: %d\n", ret);
	}

	return ret;
}

static int aicwf_vendor_get_ver(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type;
	const struct nlattr *iter;
	int payload = 0;
	char version[128];
	int  attr = -1;
	struct sk_buff *reply;

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case LOGGER_ATTRIBUTE_DRIVER_VER:
			memcpy(version, RWNX_VERS_BANNER, sizeof(RWNX_VERS_BANNER));
			payload = strlen(version);
			attr = LOGGER_ATTRIBUTE_DRIVER_VER;
			break;
		case LOGGER_ATTRIBUTE_FW_VER:
			memcpy(version, wiphy->fw_version, sizeof(wiphy->fw_version));
			payload = strlen(version);
			attr = LOGGER_ATTRIBUTE_FW_VER;
			break;
		default:
			AICWFDBG(LOGERROR, "%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}

	if (attr < 0)
		return -EINVAL;

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	if (nla_put(reply, attr,
		    payload, version)) {
		wiphy_err(wiphy, "%s put version error\n", __func__);
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "%s reply cmd error\n", __func__);
	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int aicwf_vendor_subcmd_get_channel_list(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type;
	const struct nlattr *iter;
	struct sk_buff *reply;
	int num_channels = 0;
	int *channel_list = NULL;
	int payload;
	int i = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct ieee80211_supported_band *rwnx_band_2GHz = rwnx_hw->wiphy->bands[NL80211_BAND_2GHZ];
	struct ieee80211_supported_band *rwnx_band_5GHz = rwnx_hw->wiphy->bands[NL80211_BAND_5GHZ];

	num_channels += rwnx_band_2GHz->n_channels;
	num_channels += (rwnx_hw->band_5g_support) ? rwnx_band_5GHz->n_channels : 0;

	channel_list = (int *)kzalloc(sizeof(int) * num_channels, GFP_KERNEL);
	if (!channel_list)
		return -ENOMEM;

	for (i = 0; i < rwnx_band_2GHz->n_channels; i++)
		channel_list[i] = rwnx_band_2GHz->channels[i].center_freq;

	for (; rwnx_hw->band_5g_support && i < num_channels; i++)
		channel_list[i] = rwnx_band_5GHz->channels[i].center_freq;

	payload = sizeof(num_channels) + sizeof(int) * num_channels + 4;

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case GSCAN_ATTRIBUTE_BAND:
			reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

			if (!reply)
				return -ENOMEM;

			if (nla_put_u32(reply, GSCAN_ATTRIBUTE_NUM_CHANNELS, num_channels))
				goto out_put_fail;

			if (nla_put(reply, GSCAN_ATTRIBUTE_CHANNEL_LIST, sizeof(int) * num_channels, channel_list))
				goto out_put_fail;

			ret = cfg80211_vendor_cmd_reply(reply);
			if (ret)
				wiphy_err(wiphy, "%s reply cmd error\n", __func__);
			break;
		default:
			pr_err("%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}

	kfree(channel_list);
	return ret;

out_put_fail:
	kfree(channel_list);
	kfree_skb(reply);
	return -EMSGSIZE;
}

struct ieee80211_regdomain *getRegdomainFromRwnxDB(struct wiphy *wiphy, char *alpha2);

static int aicwf_vendor_subcmd_set_country_code(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type;
	const struct nlattr *iter;
    struct ieee80211_regdomain *regdomain;

	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	if (!rwnx_hw->mod_params->custregd) {
		AICWFDBG(LOGERROR, "%s: invalid custregd\n", __func__);
		return -EINVAL;
	}

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case ANDR_WIFI_ATTRIBUTE_COUNTRY:
			printk("%s(%d), ANDR_WIFI_ATTRIBUTE_COUNTRY: %s\n", __func__, __LINE__, (char *)nla_data(iter));
            regdomain = getRegdomainFromRwnxDB(wiphy, (char *)nla_data(iter));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
        if((ret = regulatory_set_wiphy_regd(wiphy, regdomain))){
            AICWFDBG(LOGERROR, "regulatory_set_wiphy_regd fail \r\n");
        }
#else
            wiphy_apply_custom_regulatory(wiphy, regdomain);
#endif
			break;
		default:
			pr_err("%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}

	/* TODO
	 * Add handle in the future!
	 */

	return ret;
}

static int aicwf_vendor_logger_trigger_memory_dump(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	/* TODO
	 * Add handle in the future!
	 */
	return 0;
}

static int aicwf_vendor_subcmd_get_feature_set(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t feature = 0, payload;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	payload = sizeof(feature);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	/* TODO
	 * Add handle in the future!
	 */
	/*bit 1:Basic infrastructure mode*/
	if (wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION))
		feature |= WIFI_FEATURE_INFRA;

	/*bit 2:Support for 5 GHz Band*/
	if (rwnx_hw->band_5g_support)
		feature |= WIFI_FEATURE_INFRA_5G;

	/*bit3:HOTSPOT is a supplicant feature, enable it by default*/
	feature |= WIFI_FEATURE_HOTSPOT;

	/*bit 4:P2P*/
	if ((wiphy->interface_modes & BIT(NL80211_IFTYPE_P2P_CLIENT)) &&
		(wiphy->interface_modes & BIT(NL80211_IFTYPE_P2P_GO)))
		feature |= WIFI_FEATURE_P2P;

	/*bit 5:soft AP feature supported*/
	if (wiphy->interface_modes & BIT(NL80211_IFTYPE_AP))
		feature |= WIFI_FEATURE_SOFT_AP;

	/*bit 18:WiFi Logger*/
	feature |= WIFI_FEATURE_LOGGER;

	/*bit 21:WiFi mkeep_alive*/
	feature |= WIFI_FEATURE_MKEEP_ALIVE;

	if (nla_put_u32(reply, ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET, feature)) {
		wiphy_err(wiphy, "%s put u32 error\n", __func__);
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "%s reply cmd error\n", __func__);

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int aicwf_vendor_logger_get_feature(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t feature = 0, payload;

	payload = sizeof(feature);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	feature |= WIFI_LOGGER_MEMORY_DUMP_SUPPORTED;
	feature |= WIFI_LOGGER_CONNECT_EVENT_SUPPORTED;

	/*vts will test wake reason state function*/
	feature |= WIFI_LOGGER_WAKE_LOCK_SUPPORTED;

	if (nla_put_u32(reply, ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET, feature)) {
		wiphy_err(wiphy, "put skb u32 failed\n");
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "reply cmd error\n");

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int aicwf_vendor_logger_get_ring_status(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t payload;
	uint32_t ring_buffer_nums = sizeof(ring_buffer) / sizeof(ring_buffer[0]);

	payload = sizeof(ring_buffer_nums) + sizeof(ring_buffer);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	if (nla_put_u32(reply, LOGGER_ATTRIBUTE_RING_NUM, ring_buffer_nums)) {
		wiphy_err(wiphy, "put skb u32 failed\n");
		goto out_put_fail;
	}

	if (nla_put(reply, LOGGER_ATTRIBUTE_RING_STATUS, sizeof(ring_buffer), ring_buffer)) {
		wiphy_err(wiphy, "put skb failed\n");
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "reply cmd error\n");

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int aicwf_vendor_logger_start_logging(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type, intval, size, i;
	const struct nlattr *iter;
	struct wifi_ring_buffer_status rb;

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case LOGGER_ATTRIBUTE_LOG_LEVEL:
			rb.verbose_level = nla_get_u32(iter);
			break;
		case LOGGER_ATTRIBUTE_RING_FLAGS:
			rb.flags = nla_get_u32(iter);
			break;
		case LOGGER_ATTRIBUTE_LOG_TIME_INTVAL:
			intval = nla_get_u32(iter);
			break;
		case LOGGER_ATTRIBUTE_LOG_MIN_DATA_SIZE:
			size = nla_get_u32(iter);
			break;
		case LOGGER_ATTRIBUTE_RING_NAME:
			strcpy(rb.name, nla_data(iter));
			break;
		default:
			AICWFDBG(LOGERROR, "%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}

	ret = -EINVAL;
	for (i = 0; i < sizeof(ring_buffer) / sizeof(ring_buffer[0]); i++) {
		if (strcmp(rb.name, ring_buffer[i].name) == 0) {
			ret = 0;
			break;
		}
	}

	/* TODO
	 * Add handle in the future
	 */

	return ret;
}

static int aicwf_vendor_logger_get_ring_data(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type, i;
	const struct nlattr *iter;
	struct wifi_ring_buffer_status rb;

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case LOGGER_ATTRIBUTE_RING_NAME:
			strcpy(rb.name, nla_data(iter));
			break;
		default:
			pr_err("%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}

	ret = -EINVAL;
	for (i = 0; i < sizeof(ring_buffer) / sizeof(ring_buffer[0]); i++) {
		if (strcmp(rb.name, ring_buffer[i].name) == 0) {
			ret = 0;
			break;
		}
	}

	/* TODO
	 * Add handle in the future
	 */

	return ret;
}

static int aicwf_vendor_logger_get_wake_reason_stats(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t payload;

	payload = sizeof(wake_reason_cnt.total_cmd_event_wake);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	/* TODO
	 * Add handle in the future
	 */
	if (nla_put_u32(reply, WAKE_STAT_ATTRIBUTE_TOTAL_CMD_EVENT, wake_reason_cnt.total_cmd_event_wake))
		goto out_put_fail;

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "reply cmd error\n");

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int aicwf_vendor_logger_get_tx_pkt_fates(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
#if 0
	int ret = 0, rem, type;
	const struct nlattr *iter;
	int fate_num;
	int fate_data;

	AICWFDBG(LOGDEBUG, "%s Enter\r\n", __func__);
	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case LOGGER_ATTRIBUTE_PKT_FATE_NUM:
			fate_num = nla_get_u32(iter);
			break;
		case LOGGER_ATTRIBUTE_PKT_FATE_DATA:
			fate_data = nla_get_u64(iter);
			break;
		default:
			AICWFDBG(LOGERROR, "%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}
#endif
	/* TODO
	 * Add handle in the future
	 */
	return 0;
}

static int aicwf_vendor_logger_get_rx_pkt_fates(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
#if 0
	int ret = 0, rem, type;
	const struct nlattr *iter;
	int fate_num;
	int fate_data;

	AICWFDBG(LOGDEBUG, "%s Enter\r\n", __func__);
	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case LOGGER_ATTRIBUTE_PKT_FATE_NUM:
			fate_num = nla_get_u32(iter);
			break;
		case LOGGER_ATTRIBUTE_PKT_FATE_DATA:
			fate_data = nla_get_u64(iter);
			break;
		default:
			AICWFDBG(LOGERROR, "%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}
#endif
	/* TODO
	 * Add handle in the future
	 */
	return 0;
}

static int aicwf_vendor_logger_start_pkt_fate_monitoring(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	/* TODO
	 * Add handle in the future
	 */
	return 0;
}

static int aicwf_vendor_apf_subcmd_get_capabilities(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ver, max_len;
	int ret = 0;
	struct sk_buff *skb = NULL;
	
	/* APF version */
	ver = 4;
	/* APF memory size limit */
    max_len = 2048;

	
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, max_len);
	
	ret = nla_put_u32(skb, APF_ATTRIBUTE_VERSION, ver);
	if (ret < 0) {
			AICWFDBG(LOGERROR, "Failed to put APF_ATTRIBUTE_VERSION, ret:%d\n", ret);
			goto fail;
	}
	ret = nla_put_u32(skb, APF_ATTRIBUTE_MAX_LEN, max_len);
	if (ret < 0) {
			AICWFDBG(LOGERROR, "Failed to put APF_ATTRIBUTE_MAX_LEN, ret:%d\n", ret);
			goto fail;
	}

	ret = cfg80211_vendor_cmd_reply(skb);
	if (unlikely(ret)) {
			AICWFDBG(LOGERROR, "vendor command reply failed, ret=%d\n", ret);
	}

fail:
	/* TODO
	 * Add handle in the future
	 */
	return ret;
}

static int aicwf_vendor_sub_cmd_set_mac(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	int ret = 0, rem, type;
	const struct nlattr *iter;
	u8 mac[ETH_ALEN];

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
		case WIFI_VENDOR_ATTR_DRIVER_MAC_ADDR:
			memcpy(mac, nla_data(iter), ETH_ALEN);
			printk("%s, %02X:%02X:%02X:%02X:%02X:%02X\n", __func__,
					mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
			break;
		default:
			pr_err("%s(%d), Unknown type: %d\n", __func__, __LINE__, type);
			return -EINVAL;
		}
	}

	/* TODO
	 * Add handle in the future
	 */

	return ret;
}
#endif

static const struct nla_policy
aicwf_cfg80211_mkeep_alive_policy[MKEEP_ALIVE_ATTRIBUTE_MAX+1] = {
	[0] = {.type = NLA_UNSPEC },
	[MKEEP_ALIVE_ATTRIBUTE_ID]		= { .type = NLA_U8 },
	[MKEEP_ALIVE_ATTRIBUTE_IP_PKT]		= { .type = NLA_MSECS },
	[MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN]	= { .type = NLA_U16 },
	[MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR]	= { .type = NLA_MSECS,
							.len  = ETH_ALEN },
	[MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR]	= { .type = NLA_MSECS,
							.len  = ETH_ALEN },
	[MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC]	= { .type = NLA_U32 },
};

static const struct nla_policy
aicwf_cfg80211_logger_policy[LOGGER_ATTRIBUTE_MAX + 1] = {
	[0] = {.type = NLA_UNSPEC },
	[LOGGER_ATTRIBUTE_DRIVER_VER] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_FW_VER] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_LOG_LEVEL] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_RING_FLAGS] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_TIME_INTVAL] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_MIN_DATA_SIZE] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_RING_NAME] = { .type = NLA_STRING },
};

static const struct nla_policy
aicwf_cfg80211_subcmd_policy[GSCAN_ATTRIBUTE_MAX + 1] = {
	[0] = {.type = NLA_UNSPEC },
	[GSCAN_ATTRIBUTE_BAND] = { .type = NLA_U32 },
};

static const struct nla_policy
aicwf_cfg80211_andr_wifi_policy[ANDR_WIFI_ATTRIBUTE_MAX + 1] = {
	[0] = {.type = NLA_UNSPEC },
	[ANDR_WIFI_ATTRIBUTE_COUNTRY] = { .type = NLA_STRING },
};

static const struct nla_policy
aicwf_cfg80211_subcmd_set_mac_policy[WIFI_VENDOR_ATTR_DRIVER_MAX + 1] = {
	[0] = {.type = NLA_UNSPEC },
	[WIFI_VENDOR_ATTR_DRIVER_MAC_ADDR] = { .type = NLA_MSECS, .len  = ETH_ALEN },
};

/* Valori iniziali inviati al firmware a ogni avvio; a caldo si cambiano col comando vendor COEX_SET. */
static bool coex_bt_on = true;
module_param(coex_bt_on, bool, 0444);
static bool coex_disable_coexnull;
module_param(coex_disable_coexnull, bool, 0444);
static bool coex_enable_nullcts = true;
module_param(coex_enable_nullcts, bool, 0444);
static bool coex_enable_periodic_timer;
module_param(coex_enable_periodic_timer, bool, 0444);
static bool coex_timeslot_set;
module_param(coex_timeslot_set, bool, 0444);
static uint coex_timeslot0;
module_param(coex_timeslot0, uint, 0444);
static uint coex_timeslot1;
module_param(coex_timeslot1, uint, 0444);

static int aic_coex_put(struct sk_buff *skb, const struct aic_coex_cfg *c)
{
	return nla_put_u8(skb, AIC_ATTR_COEX_BT_ON, c->bt_on) ||
	       nla_put_u8(skb, AIC_ATTR_COEX_DISABLE_COEXNULL, c->disable_coexnull) ||
	       nla_put_u8(skb, AIC_ATTR_COEX_ENABLE_NULLCTS, c->enable_nullcts) ||
	       nla_put_u8(skb, AIC_ATTR_COEX_ENABLE_PERIODIC_TIMER, c->enable_periodic_timer) ||
	       nla_put_u8(skb, AIC_ATTR_COEX_TIMESLOT_SET, c->timeslot_set) ||
	       nla_put_u32(skb, AIC_ATTR_COEX_TIMESLOT0, c->timeslot[0]) ||
	       nla_put_u32(skb, AIC_ATTR_COEX_TIMESLOT1, c->timeslot[1]) ||
	       nla_put_u8(skb, AIC_ATTR_COEX_APPLIED, c->applied);
}

static int aic_vendor_reply_coex(struct wiphy *wiphy, const struct aic_coex_cfg *c)
{
	struct sk_buff *reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 128);

	if (!reply)
		return -ENOMEM;
	if (aic_coex_put(reply, c)) {
		kfree_skb(reply);
		return -EMSGSIZE;
	}
	return cfg80211_vendor_cmd_reply(reply);
}

static int aic_vendor_coex_get(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	return aic_vendor_reply_coex(wiphy, &rwnx_hw->coex);
}

static int aic_vendor_coex_set(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct aic_coex_cfg cfg = rwnx_hw->coex;
	const struct nlattr *iter;
	int rem, ret;

	nla_for_each_attr(iter, data, len, rem) {
		u8 *flag = NULL;

		switch (nla_type(iter)) {
		case AIC_ATTR_COEX_BT_ON:			flag = &cfg.bt_on; break;
		case AIC_ATTR_COEX_DISABLE_COEXNULL:		flag = &cfg.disable_coexnull; break;
		case AIC_ATTR_COEX_ENABLE_NULLCTS:		flag = &cfg.enable_nullcts; break;
		case AIC_ATTR_COEX_ENABLE_PERIODIC_TIMER:	flag = &cfg.enable_periodic_timer; break;
		case AIC_ATTR_COEX_TIMESLOT_SET:		flag = &cfg.timeslot_set; break;
		case AIC_ATTR_COEX_TIMESLOT0:
		case AIC_ATTR_COEX_TIMESLOT1:
			if (nla_len(iter) != sizeof(u32))
				return -EINVAL;
			cfg.timeslot[nla_type(iter) == AIC_ATTR_COEX_TIMESLOT1] = nla_get_u32(iter);
			break;
		default:
			return -EINVAL;
		}
		if (flag) {
			if (nla_len(iter) != sizeof(u8) || nla_get_u8(iter) > 1)
				return -EINVAL;
			*flag = nla_get_u8(iter);
		}
	}

	/* a firmware spento il valore vale dal prossimo avvio (rwnx_open) */
	cfg.applied = false;
	if (rwnx_hw->vif_started) {
		ret = rwnx_send_coex_req(rwnx_hw, &cfg);
		if (ret)
			return ret;
		cfg.applied = true;
	}
	rwnx_hw->coex = cfg;

	return aic_vendor_reply_coex(wiphy, &rwnx_hw->coex);
}

static int aic_vendor_get_info(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct sk_buff *reply;
	s8_l temp = 0;
	bool have_temp = false;

	if (rwnx_hw->vif_started && !rwnx_send_get_temp_req(rwnx_hw, &temp))
		have_temp = true;

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 256);
	if (!reply)
		return -ENOMEM;

	if (nla_put_string(reply, AIC_ATTR_DRIVER_VERSION, RWNX_VERS_BANNER) ||
	    nla_put_string(reply, AIC_ATTR_FW_VERSION, rwnx_hw->fw_version_full) ||
	    nla_put_u8(reply, AIC_ATTR_CHIP_ID, rwnx_hw->usbdev->chipid) ||
	    nla_put_u8(reply, AIC_ATTR_BAND_5G, rwnx_hw->band_5g_support) ||
	    nla_put_u8(reply, AIC_ATTR_IF_RUNNING, rwnx_hw->vif_started) ||
	    (have_temp && nla_put_u8(reply, AIC_ATTR_TEMP_C, temp))) {
		kfree_skb(reply);
		return -EMSGSIZE;
	}
	return cfg80211_vendor_cmd_reply(reply);
}

static u32 aic_chan_width_mhz(enum nl80211_chan_width w)
{
	switch (w) {
	case NL80211_CHAN_WIDTH_5:	return 5;
	case NL80211_CHAN_WIDTH_10:	return 10;
	case NL80211_CHAN_WIDTH_40:	return 40;
	case NL80211_CHAN_WIDTH_80:	return 80;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:	return 160;
	default:			return 20;
	}
}

static int aic_vendor_get_stats(struct wiphy *wiphy, struct wireless_dev *wdev,
	const void *data, int len)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(wdev->netdev);
	struct rtnl_link_stats64 st;
	struct mm_get_sta_info_cfm sta = {0};
	struct sk_buff *reply;
	bool have_sta = false, have_chan = false;
	struct cfg80211_chan_def *cd = NULL;

	dev_get_stats(wdev->netdev, &st);

	if (rwnx_hw->chanctx_table[rwnx_hw->cur_chanctx].count) {
		cd = &rwnx_hw->chanctx_table[rwnx_hw->cur_chanctx].chan_def;
		have_chan = cd->chan;
	}
	if (vif->sta.ap && wdev->iftype == NL80211_IFTYPE_STATION)
		have_sta = !rwnx_send_get_sta_info_req(rwnx_hw, vif->sta.ap->sta_idx, &sta);

	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 512);
	if (!reply)
		return -ENOMEM;

#define PUT64(attr, v) nla_put_u64_64bit(reply, attr, v, AIC_ATTR_PAD)
	if (PUT64(AIC_ATTR_NET_RX_PACKETS, st.rx_packets) ||
	    PUT64(AIC_ATTR_NET_TX_PACKETS, st.tx_packets) ||
	    PUT64(AIC_ATTR_NET_RX_BYTES, st.rx_bytes) ||
	    PUT64(AIC_ATTR_NET_TX_BYTES, st.tx_bytes) ||
	    PUT64(AIC_ATTR_NET_RX_ERRORS, st.rx_errors) ||
	    PUT64(AIC_ATTR_NET_TX_ERRORS, st.tx_errors) ||
	    PUT64(AIC_ATTR_NET_RX_DROPPED, st.rx_dropped) ||
	    PUT64(AIC_ATTR_NET_TX_DROPPED, st.tx_dropped))
		goto fail;
#undef PUT64

	if (have_chan &&
	    (nla_put_u32(reply, AIC_ATTR_CHAN_FREQ, cd->chan->center_freq) ||
	     nla_put_u32(reply, AIC_ATTR_CHAN_CENTER_FREQ1, cd->center_freq1) ||
	     nla_put_u32(reply, AIC_ATTR_CHAN_WIDTH, aic_chan_width_mhz(cd->width))))
		goto fail;

	if (have_sta &&
	    (nla_put_u8(reply, AIC_ATTR_STA_RSSI, sta.rssi) ||
	     nla_put_u32(reply, AIC_ATTR_STA_RATE_INFO, sta.rate_info) ||
	     nla_put_u32(reply, AIC_ATTR_STA_TX_FAILED, sta.txfailed) ||
	     nla_put_u32(reply, AIC_ATTR_STA_CHAN_TIME, sta.chan_time) ||
	     nla_put_u32(reply, AIC_ATTR_STA_CHAN_BUSY_TIME, sta.chan_busy_time) ||
	     nla_put_u32(reply, AIC_ATTR_STA_CHAN_TX_BUSY_TIME, sta.chan_tx_busy_time) ||
	     nla_put_u32(reply, AIC_ATTR_STA_ACK_FAIL, sta.ack_fail_stat) ||
	     nla_put_u32(reply, AIC_ATTR_STA_ACK_SUCC, sta.ack_succ_stat)))
		goto fail;

	return cfg80211_vendor_cmd_reply(reply);
fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

void aicwf_vendor_event(struct rwnx_hw *rwnx_hw, enum aic_vendor_event ev)
{
	struct sk_buff *skb;

	skb = cfg80211_vendor_event_alloc(rwnx_hw->wiphy, NULL, 0, ev, GFP_ATOMIC);
	if (skb)
		cfg80211_vendor_event(skb, GFP_ATOMIC);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
static int aicwf_dump_interface(struct wiphy *wiphy,
				struct wireless_dev *wdev, struct sk_buff *skb,
				const void *data, int data_len,
				unsigned long *storage)
{
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)

const struct wiphy_vendor_command aicwf_vendor_cmd[] = {
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = WIFI_OFFLOAD_SUBCMD_START_MKEEP_ALIVE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_start_mkeep_alive,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_mkeep_alive_policy,
		.maxattr = MKEEP_ALIVE_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = WIFI_OFFLOAD_SUBCMD_STOP_MKEEP_ALIVE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_stop_mkeep_alive,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_mkeep_alive_policy,
		.maxattr = MKEEP_ALIVE_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_VER
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_get_ver,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_logger_policy,
		.maxattr = LOGGER_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = GSCAN_SUBCMD_GET_CHANNEL_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_subcmd_get_channel_list,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_subcmd_policy,
		.maxattr = GSCAN_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = WIFI_SUBCMD_SET_COUNTRY_CODE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_subcmd_set_country_code,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_andr_wifi_policy,
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_TRIGGER_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_trigger_memory_dump,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = WIFI_SUBCMD_GET_FEATURE_SET
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_subcmd_get_feature_set,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_FEATURE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_get_feature,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_RING_STATUS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_get_ring_status,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_START_LOGGING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_start_logging,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_logger_policy,
		.maxattr = LOGGER_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_RING_DATA
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_get_ring_data,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_logger_policy,
		.maxattr = LOGGER_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_WAKE_REASON_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_get_wake_reason_stats,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = APF_SUBCMD_GET_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_apf_subcmd_get_capabilities,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_START_PKT_FATE_MONITORING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_start_pkt_fate_monitoring,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_TX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_get_tx_pkt_fates,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = LOGGER_GET_RX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |  WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = aicwf_vendor_logger_get_rx_pkt_fates,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = GOOGLE_OUI,
			.subcmd = VENDOR_NL80211_SUBCMD_SET_MAC
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = aicwf_vendor_sub_cmd_set_mac,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
		.dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = aicwf_cfg80211_subcmd_set_mac_policy,
		.maxattr = WIFI_VENDOR_ATTR_DRIVER_MAX,
#endif
    },
    {
        {
         .vendor_id = BRCM_OUI,
         .subcmd = VENDOR_NL80211_SUBCMD_SET_MAC
        },
        .flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_RUNNING,
        .doit = aicwf_vendor_sub_cmd_set_mac,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
        .dumpit = aicwf_dump_interface,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
        .policy = aicwf_cfg80211_subcmd_set_mac_policy,
        .maxattr = WIFI_VENDOR_ATTR_DRIVER_MAX,
#endif
    },
	{
		{
			.vendor_id = AIC_VENDOR_OUI,
			.subcmd = AIC_VENDOR_SUBCMD_COEX_GET
		},
		.flags = 0,
		.doit = aic_vendor_coex_get,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = AIC_VENDOR_OUI,
			.subcmd = AIC_VENDOR_SUBCMD_COEX_SET
		},
		.flags = 0,
		.doit = aic_vendor_coex_set,
		/* con una policy il kernel pretende dati NLA_F_NESTED, che `iw vendor send` non marca */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = AIC_VENDOR_OUI,
			.subcmd = AIC_VENDOR_SUBCMD_GET_INFO
		},
		.flags = 0,
		.doit = aic_vendor_get_info,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = AIC_VENDOR_OUI,
			.subcmd = AIC_VENDOR_SUBCMD_GET_STATS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = aic_vendor_get_stats,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
static const struct nl80211_vendor_cmd_info aicwf_vendor_events[] = {
	[AIC_VENDOR_EVENT_COEX_TS_TIMEOUT] = { .vendor_id = AIC_VENDOR_OUI, .subcmd = AIC_VENDOR_EVENT_COEX_TS_TIMEOUT },
	[AIC_VENDOR_EVENT_FW_PANIC]        = { .vendor_id = AIC_VENDOR_OUI, .subcmd = AIC_VENDOR_EVENT_FW_PANIC },
	[AIC_VENDOR_EVENT_FW_ASSERT]       = { .vendor_id = AIC_VENDOR_OUI, .subcmd = AIC_VENDOR_EVENT_FW_ASSERT },
};
#endif

int aicwf_vendor_init(struct wiphy *wiphy)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	rwnx_hw->coex = (struct aic_coex_cfg) {
		.bt_on = coex_bt_on,
		.disable_coexnull = coex_disable_coexnull,
		.enable_nullcts = coex_enable_nullcts,
		.enable_periodic_timer = coex_enable_periodic_timer,
		.timeslot_set = coex_timeslot_set,
		.timeslot = { coex_timeslot0, coex_timeslot1 },
	};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	wiphy->vendor_commands = aicwf_vendor_cmd;
	wiphy->n_vendor_commands = ARRAY_SIZE(aicwf_vendor_cmd);
	wiphy->vendor_events = aicwf_vendor_events;
	wiphy->n_vendor_events = ARRAY_SIZE(aicwf_vendor_events);
#endif
	return 0;
}
