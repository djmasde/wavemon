/*
 * Auxiliary declarations and functions imported from iwlib in order to
 * process and parse scan events. This code is copied with little change
 * from wireless tools 30. It remains here until the wext code will be
 * replaced by corresponding netlink calls.
 */
#include "iw_if.h"
#include <search.h>		/* lsearch(3) */

#define MAX_SCAN_WAIT	15000	/* maximum milliseconds spent waiting */
/*MAX_SCAN_WAIT to 15000 (runs ok with ath9k driver with "firmware libre") */

/*
 * Meta-data about all the additional standard Wireless Extension events
 * we know about.
 */
/* Type of headers we know about (basically union iwreq_data) */
#define IW_HEADER_TYPE_NULL	0	/* Not available */
#define IW_HEADER_TYPE_CHAR	2	/* char [IFNAMSIZ] */
#define IW_HEADER_TYPE_UINT	4	/* __u32 */
#define IW_HEADER_TYPE_FREQ	5	/* struct iw_freq */
#define IW_HEADER_TYPE_ADDR	6	/* struct sockaddr */
#define IW_HEADER_TYPE_POINT	8	/* struct iw_point */
#define IW_HEADER_TYPE_PARAM	9	/* struct iw_param */
#define IW_HEADER_TYPE_QUAL	10	/* struct iw_quality */

/* Size (in bytes) of various events */
static const int event_type_size[] = {
	[IW_HEADER_TYPE_NULL]  = IW_EV_LCP_PK_LEN,
	[IW_HEADER_TYPE_CHAR]  = IW_EV_CHAR_PK_LEN,
	[IW_HEADER_TYPE_UINT]  = IW_EV_UINT_PK_LEN,
	[IW_HEADER_TYPE_FREQ]  = IW_EV_FREQ_PK_LEN,
	[IW_HEADER_TYPE_ADDR]  = IW_EV_ADDR_PK_LEN,
	/*
	 * Fix IW_EV_POINT_PK_LEN: some wireless.h versions define this
	 * erroneously as IW_EV_LCP_LEN + 4 (e.g. ESSID will disappear).
	 * The value below is from wireless tools 30.
	 */
	[IW_HEADER_TYPE_POINT] = IW_EV_LCP_PK_LEN + 4,
	[IW_HEADER_TYPE_PARAM] = IW_EV_PARAM_PK_LEN,
	[IW_HEADER_TYPE_QUAL]  = IW_EV_QUAL_PK_LEN
};

/* Handling flags */
#define IW_DESCR_FLAG_NONE	0x0000	/* Obvious */
/* Wrapper level flags */
#define IW_DESCR_FLAG_DUMP	0x0001	/* Not part of the dump command */
#define IW_DESCR_FLAG_EVENT	0x0002	/* Generate an event on SET */
#define IW_DESCR_FLAG_RESTRICT	0x0004	/* GET : request is ROOT only */
				/* SET : Omit payload from generated iwevent */
#define IW_DESCR_FLAG_NOMAX	0x0008	/* GET : no limit on request size */
/* Driver level flags */
#define IW_DESCR_FLAG_WAIT	0x0100	/* Wait for driver event */

struct iw_ioctl_description {
	__u8 header_type;	/* NULL, iw_point or other */
	__u8 token_type;	/* Future */
	__u16 token_size;	/* Granularity of payload */
	__u16 min_tokens;	/* Min acceptable token number */
	__u16 max_tokens;	/* Max acceptable token number */
	__u32 flags;		/* Special handling of the request */
};

/*
 * Meta-data about all the standard Wireless Extension request we
 * know about.
 */
static const struct iw_ioctl_description standard_ioctl_descr[] = {
	[SIOCSIWCOMMIT	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWNAME	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_CHAR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWNWID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWNWID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWFREQ	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWFREQ	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWMODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWMODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWSENS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWSENS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRANGE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWRANGE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_range),
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWPRIV	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWPRIV	- SIOCIWFIRST] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCSIWSTATS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[SIOCGIWSTATS	- SIOCIWFIRST] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_NULL,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr),
		.max_tokens	= IW_MAX_SPY,
	},
	[SIOCGIWSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_SPY,
	},
	[SIOCSIWTHRSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[SIOCGIWTHRSPY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[SIOCSIWAP	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[SIOCGIWAP	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWMLME	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_mlme),
		.max_tokens	= sizeof(struct iw_mlme),
	},
	[SIOCGIWAPLIST	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_AP,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[SIOCSIWSCAN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= 0,
		.max_tokens	= sizeof(struct iw_scan_req),
	},
	[SIOCGIWSCAN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_SCAN_MAX_DATA,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[SIOCSIWESSID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[SIOCGIWESSID	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[SIOCSIWNICKN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
	},
	[SIOCGIWNICKN	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE + 1,
	},
	[SIOCSIWRATE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRATE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRTS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRTS	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWFRAG	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWFRAG	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWTXPOW	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWTXPOW	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWRETRY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWRETRY	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWENCODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_EVENT | IW_DESCR_FLAG_RESTRICT,
	},
	[SIOCGIWENCODE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_DUMP | IW_DESCR_FLAG_RESTRICT,
	},
	[SIOCSIWPOWER	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWPOWER	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#ifdef SIOCSIWMODUL
	[SIOCSIWMODUL	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#endif
#ifdef SIOCGIWMODUL
	[SIOCGIWMODUL	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
#endif
	[SIOCSIWGENIE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[SIOCGIWGENIE	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[SIOCSIWAUTH	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCGIWAUTH	- SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[SIOCSIWENCODEEXT - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[SIOCGIWENCODEEXT - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[SIOCSIWPMKSA - SIOCIWFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_pmksa),
		.max_tokens	= sizeof(struct iw_pmksa),
	},
};

static const struct iw_ioctl_description standard_event_descr[] = {
	[IWEVTXDROP - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVQUAL - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_QUAL,
	},
	[IWEVCUSTOM - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_CUSTOM_MAX,
	},
	[IWEVREGISTERED - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVEXPIRED - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IWEVGENIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVMICHAELMICFAILURE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_michaelmicfailure),
	},
	[IWEVASSOCREQIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVASSOCRESPIE - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IWEVPMKIDCAND - IWEVFIRST] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_pmkid_cand),
	},
};

struct stream_descr {
	char *current;		/* Current event in stream of events */
	char *value;		/* Current value in event */
	char *end;		/* End of the stream */
};

/*
 * Extract the next event from the event stream.
 */
static int iw_extract_event_stream(struct stream_descr *stream,
				   struct iw_event *iwe, int we_version)
{
	const struct iw_ioctl_description *descr = NULL;
	int event_type;
	unsigned int event_len = 1;	/* Invalid */
	unsigned cmd_index;	/* *MUST* be unsigned */
	char *pointer;

	if (stream->current + IW_EV_LCP_PK_LEN > stream->end)
		return 0;

	/* Extract the event header to get the event id.
	 * Note : the event may be unaligned, therefore copy... */
	memcpy((char *)iwe, stream->current, IW_EV_LCP_PK_LEN);

	if (iwe->len <= IW_EV_LCP_PK_LEN)
		return -1;

	/* Get the type and length of that event */
	if (iwe->cmd <= SIOCIWLAST) {
		cmd_index = iwe->cmd - SIOCIWFIRST;
		if (cmd_index < ARRAY_SIZE(standard_ioctl_descr))
			descr = standard_ioctl_descr + cmd_index;
	} else {
		cmd_index = iwe->cmd - IWEVFIRST;
		if (cmd_index < ARRAY_SIZE(standard_event_descr))
			descr = standard_event_descr + cmd_index;
	}

	/* Unknown events -> event_type = 0  =>  IW_EV_LCP_PK_LEN */
	event_type = descr ? descr->header_type : 0;
	event_len  = event_type_size[event_type];

	/* Check if we know about this event */
	if (event_len <= IW_EV_LCP_PK_LEN) {
		stream->current += iwe->len;			/* Skip to next event */
		return 2;
	}
	event_len -= IW_EV_LCP_PK_LEN;

	/* Fixup for earlier version of WE */
	if (we_version <= 18 && event_type == IW_HEADER_TYPE_POINT)
		event_len += IW_EV_POINT_OFF;

	if (stream->value != NULL)
		pointer = stream->value;			/* Next value in event */
	else
		pointer = stream->current + IW_EV_LCP_PK_LEN;	/* First value in event */

	/* Copy the rest of the event (at least, fixed part) */
	if (pointer + event_len > stream->end) {
		stream->current += iwe->len;			/* Skip to next event */
		return -2;
	}

	/* Fixup for WE-19 and later: pointer no longer in the stream */
	/* Beware of alignment. Dest has local alignment, not packed */
	if (we_version > 18 && event_type == IW_HEADER_TYPE_POINT)
		memcpy((char *)iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF, pointer, event_len);
	else
		memcpy((char *)iwe + IW_EV_LCP_LEN, pointer, event_len);

	/* Skip event in the stream */
	pointer += event_len;

	/* Special processing for iw_point events */
	if (event_type == IW_HEADER_TYPE_POINT) {
		unsigned int extra_len = iwe->len - (event_len + IW_EV_LCP_PK_LEN);

		if (extra_len > 0) {
			/* Set pointer on variable part (warning : non aligned) */
			iwe->u.data.pointer = pointer;

			/* Check that we have a descriptor for the command */
			if (descr == NULL) {
				/* Can't check payload -> unsafe... */
				iwe->u.data.pointer = NULL;	/* Discard paylod */
			} else {
				unsigned int token_len = iwe->u.data.length * descr->token_size;
				/*
				 * Ugly fixup for alignment issues.
				 * If the kernel is 64 bits and userspace 32 bits, we have an extra 4 + 4
				 * bytes. Fixing that in the kernel would break 64 bits userspace.
				 */
				if (token_len != extra_len && extra_len >= 4) {
					union iw_align_u16 {
						__u16 value;
						unsigned char byte[2];
					} alt_dlen;
					unsigned int alt_token_len;

					/* Userspace seems to not always like unaligned access,
					 * so be careful and make sure to align value.
					 * I hope gcc won't play any of its aliasing tricks... */
					alt_dlen.byte[0] = *(pointer);
					alt_dlen.byte[1] = *(pointer + 1);
					alt_token_len = alt_dlen.value * descr->token_size;

					/* Verify that data is consistent if assuming 64 bit alignment... */
					if (alt_token_len + 8 == extra_len) {

						/* Ok, let's redo everything */
						pointer -= event_len;
						pointer += 4;

						/* Dest has local alignment, not packed */
						memcpy((char *)iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF, pointer, event_len);
						pointer += event_len + 4;
						token_len = alt_token_len;

						/* We may have no payload */
						if (alt_token_len)
							iwe->u.data.pointer = pointer;
						else
							iwe->u.data.pointer = NULL;
					}
				}

				/* Discard bogus events which advertise more tokens than they carry ... */
				if (token_len > extra_len)
					iwe->u.data.pointer = NULL;	/* Discard paylod */

				/* Check that the advertised token size is not going to
				 * produce buffer overflow to our caller... */
				if (iwe->u.data.length > descr->max_tokens
				    && !(descr->flags & IW_DESCR_FLAG_NOMAX))
					iwe->u.data.pointer = NULL;	/* Discard payload */

				/* Same for underflows... */
				if (iwe->u.data.length < descr->min_tokens)
					iwe->u.data.pointer = NULL;	/* Discard paylod */
			}
		} else {
			/* No data */
			iwe->u.data.pointer = NULL;
		}

		stream->current += iwe->len;			/* Go to next event */
	} else {
		/*
		 * Ugly fixup for alignment issues.
		 * If the kernel is 64 bits and userspace 32 bits, we have an extra 4 bytes.
		 * Fixing that in the kernel would break 64 bits userspace.
		 */
		if (stream->value == NULL &&
		    ((iwe->len - IW_EV_LCP_PK_LEN) % event_len == 4 ||
		     (iwe->len == 12 && (event_type == IW_HEADER_TYPE_UINT ||
					 event_type == IW_HEADER_TYPE_QUAL)))) {

			pointer -= event_len;
			pointer += 4;

			/* Beware of alignment. Dest has local alignment, not packed */
			memcpy((char *)iwe + IW_EV_LCP_LEN, pointer, event_len);
			pointer += event_len;
		}

		if (pointer + event_len <= stream->current + iwe->len) {
			stream->value = pointer;		/* Go to next value */
		} else {
			stream->value = NULL;
			stream->current += iwe->len;		/* Go to next event */
		}
	}
	return 1;
}

static void iw_extract_ie(struct iw_event *iwe, struct scan_entry *sr)
{
	const uint8_t wpa1_oui[3] = { 0x00, 0x50, 0xf2 };
	uint8_t *buffer = iwe->u.data.pointer;
	int ielen = 0, ietype, i;

	/* Loop on each IE, each is min. 2 bytes TLV: IE-ID - Length - Value */
	for (i = 0; i <= iwe->u.data.length - 2;  i += ielen + 2) {
		ietype = buffer[i];
		ielen  = buffer[i + 1];

		switch (ietype) {
		case 0x30:
			if (ielen < 4)	/* make sure we have enough data */
				continue;
			sr->flags |= IW_ENC_CAPA_WPA2;
			break;
		case 0xdd:
			/* Not all IEs that start with 0xdd are WPA1 */
			if (ielen < 8 || memcmp(buffer + i + 2, wpa1_oui, 3) ||
			    buffer[i + 5] != 1)
				continue;
			sr->flags |= IW_ENC_CAPA_WPA;
			break;
		}
	}
}
/*----------------- End of code copied from iwlib -----------------------*/

/*
 * Ordering functions for scan results: all return true for a < b.
 */

/* Order by frequency. */
static bool cmp_freq(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->freq < b->freq;
}

/* Order by signal strength. */
static bool cmp_sig(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->qual.level < b->qual.level;
}

/* Order by ESSID, organize entries with same ESSID by frequency and signal. */
static bool cmp_essid(const struct scan_entry *a, const struct scan_entry *b)
{
	int res = strncmp(a->essid, b->essid, IW_ESSID_MAX_SIZE);

	return res == 0 ? (a->freq == b->freq ? cmp_sig(a, b) : cmp_freq(a, b))
			: res < 0;
}

/* Order by frequency, grouping channels by ESSID. */
static bool cmp_chan(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->freq == b->freq ? cmp_essid(a, b) : cmp_freq(a, b);
}

/* Order by frequency first, then by signal strength. */
static bool cmp_chan_sig(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->freq == b->freq ? cmp_sig(a, b) : cmp_chan(a, b);
}

/* Order by openness (open access points frist). */
static bool cmp_open(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->has_key < b->has_key;
}

/* Sort (open) access points by signal strength. */
static bool cmp_open_sig(const struct scan_entry *a, const struct scan_entry *b)
{
	return a->has_key == b->has_key ? cmp_sig(a, b) : cmp_open(a, b);
}

static bool (*scan_cmp[])(const struct scan_entry *, const struct scan_entry *) = {
	[SO_CHAN]	= cmp_chan,
	[SO_SIGNAL]	= cmp_sig,
	[SO_ESSID]	= cmp_essid,
	[SO_OPEN]	= cmp_open,
	[SO_CHAN_SIG]	= cmp_chan_sig,
	[SO_OPEN_SIG]	= cmp_open_sig
};

/**
 * Produce ranked list of scan results.
 * @ifname:     interface name to run scan on
 * @we_version: version of the WE extensions (needed internally)
 */
static struct scan_entry *get_scan_list(const char *ifname, int we_version)
{
	struct scan_entry *head = NULL, **tailp = &head;
	struct iwreq wrq;
	int wait, waited = 0;
	int skfd = socket(AF_INET, SOCK_DGRAM, 0);

	if (skfd < 0)
		err_sys("%s: can not open socket", __func__);
	/*
	 * Some drivers may return very large scan results, either because there
	 * are many cells, or there are many large elements. Do not bother to
	 * guess buffer size, use maximum u16 wrq.u.data.length size.
	 */
	char scan_buf[0xffff];

	/* We are checking errno when returning NULL, so reset it here */
	errno = 0;

	memset(&wrq, 0, sizeof(wrq));
	strncpy(wrq.ifr_ifrn.ifrn_name, ifname, IFNAMSIZ);
	if (ioctl(skfd, SIOCSIWSCAN, &wrq) < 0)
		goto done;

	/* Larger initial timeout of 250ms between set and first get */
	for (wait = 250; (waited += wait) < MAX_SCAN_WAIT; wait = 100) {
		struct timeval tv = { 0, wait * 1000 };

		while (select(0, NULL, NULL, NULL, &tv) < 0)
			if (errno != EINTR && errno != EAGAIN)
				return NULL;

		wrq.u.data.pointer = scan_buf;
		wrq.u.data.length  = sizeof(scan_buf);
		wrq.u.data.flags   = 0;

		if (ioctl(skfd, SIOCGIWSCAN, &wrq) == 0)
			break;
	}

	if (wrq.u.data.length) {
		struct iw_event iwe;
		struct stream_descr stream;
		struct scan_entry *new = NULL;
		int f = 0;		/* Idea taken from waproamd */

		memset(&stream, 0, sizeof(stream));
		stream.current = scan_buf;
		stream.end     = scan_buf + wrq.u.data.length;

		while (iw_extract_event_stream(&stream, &iwe, we_version) > 0) {
        		if (!new)
				new = calloc(1, sizeof(*new));

			switch (iwe.cmd) {
			case SIOCGIWAP:
                		f = 1;
				memcpy(&new->ap_addr, &iwe.u.ap_addr.sa_data, sizeof(new->ap_addr));
				break;
			case SIOCGIWESSID:
                		f |= 2;
				memset(new->essid, 0, sizeof(new->essid));

				if (iwe.u.essid.flags && iwe.u.essid.pointer && iwe.u.essid.length)
					memcpy(new->essid, iwe.u.essid.pointer, iwe.u.essid.length);
				break;
			case SIOCGIWMODE:
				new->mode = iwe.u.mode;
                    		f |= 4;
				break;
			case SIOCGIWFREQ:
                		f |= 8;
				new->freq = freq_to_hz(&iwe.u.freq);
				break;
			case SIOCGIWENCODE:
                		f |= 16;
				new->has_key = !(iwe.u.data.flags & IW_ENCODE_DISABLED);
				break;
			case IWEVQUAL:
				f |= 32;
				memcpy(&new->qual, &iwe.u.qual, sizeof(struct iw_quality));
				break;
			case IWEVGENIE:
				f |= 64;
				iw_extract_ie(&iwe, new);
				break;
			}
			if (f == 127) {
				f      = 0;
				*tailp = new;
				tailp  = &new->next;
				new    = NULL;
			}
		}
		free(new);	/* may have been allocated, but not filled in */
	}
done:
	close(skfd);
	return head;
}

/*
 * Simple sort routine.
 * FIXME: use hash or tree to store entries, a list to display them.
 */
void sort_scan_list(struct scan_entry **headp)
{
	struct scan_entry *head = NULL, *cur, *new = *headp, **prev;

	while (new) {
		for (cur = head, prev = &head; cur &&
		     conf.scan_sort_asc == scan_cmp[conf.scan_sort_order](cur, new);
		     prev = &cur->next, cur = cur->next)
			;
		*prev = new;
		new = new->next;
		(*prev)->next = cur;
	}
	*headp = head;
}

static void free_scan_list(struct scan_entry *head)
{
	if (head) {
		free_scan_list(head->next);
		free(head);
	}
}

/*
 * 	Channel statistics shown at the bottom of scan screen.
 */

/*
 * For lsearch, it compares key value with array member, needs to
 * return 0 if they are the same, non-0 otherwise.
 */
static int cmp_key(const void *a, const void *b)
{
	return ((struct cnt *)a)->val - ((struct cnt *)b)->val;
}

/* For quick-sorting the array in descending order of counts */
static int cmp_cnt(const void *a, const void *b)
{
	if (conf.scan_sort_order == SO_CHAN && !conf.scan_sort_asc)
		return ((struct cnt *)a)->count - ((struct cnt *)b)->count;
	return ((struct cnt *)b)->count - ((struct cnt *)a)->count;
}

/**
 * Fill in sr->channel_stats (must not have been allocated yet).
 */
static void compute_channel_stats(struct scan_result *sr)
{
	struct scan_entry *cur;
	struct cnt *bin, key = {0, 0};
	size_t n = 0;

	if (!sr->num.entries)
		return;

	sr->channel_stats = calloc(sr->num.entries, sizeof(key));
	for (cur = sr->head; cur; cur = cur->next) {
		if (cur->chan >= 0) {
			key.val = cur->chan;
			bin = lsearch(&key, sr->channel_stats, &n, sizeof(key), cmp_key);
			if (bin)
				bin->count++;
		}
	}

	if (n > 0) {
		qsort(sr->channel_stats, n, sizeof(key), cmp_cnt);
	} else {
		free(sr->channel_stats);
		sr->channel_stats = NULL;
	}
	sr->num.ch_stats = n < MAX_CH_STATS ? n : MAX_CH_STATS;
}

/*
 *	Scan results.
 */
void scan_result_init(struct scan_result *sr)
{
	memset(sr, 0, sizeof(*sr));
	iw_getinf_range(conf_ifname(), &sr->range);
	pthread_mutex_init(&sr->mutex, NULL);
}

void scan_result_fini(struct scan_result *sr)
{
	free_scan_list(sr->head);
	free(sr->channel_stats);
	pthread_mutex_destroy(&sr->mutex);
}

/** The actual scan thread. */
void *do_scan(void *sr_ptr)
{
	struct scan_result *sr = (struct scan_result *)sr_ptr;
	struct scan_entry *cur;

	pthread_detach(pthread_self());

	do {
		pthread_mutex_lock(&sr->mutex);

		free_scan_list(sr->head);
		free(sr->channel_stats);

		sr->head          = NULL;
		sr->channel_stats = NULL;
		sr->msg[0]        = '\0';
		sr->max_essid_len = MAX_ESSID_LEN;
		memset(&(sr->num), 0, sizeof(sr->num));

		sr->head = get_scan_list(conf_ifname(), sr->range.we_version_compiled);
		if (!sr->head) {
			switch(errno) {
			case EPERM:
				/* Don't try to read leftover results, it does not work reliably. */
				if (!has_net_admin_capability())
					snprintf(sr->msg, sizeof(sr->msg),
						 "This screen requires CAP_NET_ADMIN permissions");
				break;
			case EFAULT:
				/*
				 * EFAULT can occur after a window resizing event and is temporary.
				 * It may also occur when the interface is down, hence defer handling.
				 */
				break;
			case EINTR:
			case EBUSY:
			case EAGAIN:
				/* Temporary errors. */
				snprintf(sr->msg, sizeof(sr->msg), "Waiting for scan data on %s ...", conf_ifname());
				break;
			case ENETDOWN:
				snprintf(sr->msg, sizeof(sr->msg), "Interface %s is down - setting it up ...", conf_ifname());
				if (if_set_up(conf_ifname()) < 0)
					err_sys("Can not bring up interface '%s'", conf_ifname());
				break;
			case E2BIG:
				/*
				 * This is a driver issue, since already using the largest possible
				 * scan buffer. See comments in iwlist.c of wireless tools.
				 */
				snprintf(sr->msg, sizeof(sr->msg),
					 "No scan on %s: Driver returned too much data", conf_ifname());
				break;
			case 0:
				snprintf(sr->msg, sizeof(sr->msg), "Empty scan results on %s", conf_ifname());
				break;
			default:
				snprintf(sr->msg, sizeof(sr->msg),
					 "Scan failed on %s: %s", conf_ifname(), strerror(errno));
			}
		}

		for (cur = sr->head; cur; cur = cur->next) {
			if (str_is_ascii(cur->essid))
				sr->max_essid_len = clamp(strlen(cur->essid),
							  sr->max_essid_len,
							  IW_ESSID_MAX_SIZE);
			iw_sanitize(&sr->range, &cur->qual, &cur->dbm);
			cur->chan = freq_to_channel(cur->freq, &sr->range);
			if (cur->freq >= 5e9)
				sr->num.five_gig++;
			else if (cur->freq >= 2e9)
				sr->num.two_gig++;
			sr->num.entries += 1;
			sr->num.open    += !cur->has_key;
		}
		compute_channel_stats(sr);

		pthread_mutex_unlock(&sr->mutex);
	} while (usleep(conf.stat_iv * 1000) == 0);

	return NULL;
}
