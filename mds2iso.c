#define _DEFAULT_SOURCE
#include <endian.h>
#include <err.h>
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <unistd.h>
#include "mapfile.h"
#include "hexdump.h"
#include "version.h"

extern char *__progname;
static void noreturn usage(void);

enum trackmode_e {
	TM_NONE = 0,
	TM_DVD = 2,
	TM_AUDIO = 0xa9,
	TM_MODE1,
	TM_MODE2,
	TM_MODE2_FORM1,
	TM_MODE2_FORM2,
	TM_MODE2_SUB = 0xec
};

struct trackmode_info_s {
	enum trackmode_e trackmode;
	unsigned data_len;
	unsigned data_off;
	unsigned data_stride;
	bool last;
} trackmode_infos[] = {
	{ TM_NONE, 0, 0 },
	{ TM_DVD, 0x800, 0 },
	{ TM_AUDIO, 0x930, 0 },
	{ TM_MODE1, 0x800, 0x10 },
	{ TM_MODE2, 0x920, 0x10 },
	{ TM_MODE2_FORM1, 0x800, 0x18 },
	{ TM_MODE2_FORM2, 0x914, 0x18 },
	{ TM_MODE2_SUB, 0x800, 0x18 },
	{ .last = true },
};


const char *mds_mediatype_tostring(const uint16_t mediatype)
{
	switch (mediatype) {
	case 0: return "CD-ROM";
	case 1: return "CD-R";
	case 2: return "CD-RW";
	case 16: return "DVD-ROM";
	case 18: return "DVD-R";
	default: return "(unknown)";
	}
}

const char *mds_trackmode_tostring(const enum trackmode_e trackmode)
{
	switch (trackmode) {
	case TM_NONE: return "(lead-in)";
	case TM_DVD: return "DVD";
	case TM_AUDIO: return "AUDIO";
	case TM_MODE1: return "MODE1";
	case TM_MODE2: return "MODE2";
	case TM_MODE2_FORM1: return "MODE2_FORM1";
	case TM_MODE2_FORM2: return "MODE2_FORM2";
	case TM_MODE2_SUB: return "MODE2 (with subchannels)";
	default: return "(unknown)";
	}
}

bool IsCD(const uint16_t mediatype)
{
	switch (mediatype) {
	case 0:
	case 1:
	case 2:
		return true;
	default:
		return false;
	}
}

struct trackmode_info_s get_info_for_trackmode(const enum trackmode_e trackmode)
{
	for (size_t idx = 0; !trackmode_infos[idx].last; idx++) {
		if (trackmode == trackmode_infos[idx].trackmode)
			return trackmode_infos[idx];
	}
	return (struct trackmode_info_s) { .last = true };
}

struct mds_s {
	char magic[16];	// "MEDIA DESCRIPTOR"
	uint8_t version[2];
	uint16_t mediatype;
	uint16_t numsessions;
	uint32_t _idk16;
	uint16_t bca_len;
	char _idk1c[8];
	uint32_t bca_off;
	char _idc28[0x18];
	uint32_t discstruct_off;
	char _idk44[0x0c];
	uint32_t session_off;
	uint32_t dpm_off;
} __attribute__((packed));

void mds_ntoh(struct mds_s *mds)
{
	mds->mediatype = le16toh(mds->mediatype);
	mds->numsessions = le16toh(mds->numsessions);
	mds->bca_len = le16toh(mds->bca_len);
	mds->bca_off = le32toh(mds->bca_off);
	mds->discstruct_off = le32toh(mds->discstruct_off);
	mds->session_off = le32toh(mds->session_off);
	mds->dpm_off = le32toh(mds->dpm_off);
}

struct session_s {
	uint32_t sec_first;
	uint32_t sec_last;
	uint16_t numsession;
	uint8_t numtracks;
	uint8_t numtracks2;
	uint16_t track_first;
	uint16_t track_last;
	uint32_t _idk10;
	uint32_t track_off;
} __attribute__((packed));

void session_ntoh(struct session_s *session)
{
	session->sec_first = le32toh(session->sec_first);
	session->sec_last = le32toh(session->sec_last);
	session->numsession = le16toh(session->numsession);
	session->track_first = le16toh(session->track_first);
	session->track_last = le16toh(session->track_last);
	session->track_off = le32toh(session->track_off);
}

struct track_s {
	uint8_t trackmode;
	uint8_t numsubchannels;
	uint8_t adr;
	uint8_t trackno;
	uint8_t pointno;
	uint32_t _idk4;
	uint8_t minute;
	uint8_t second;
	uint8_t frame;
	/* below are zero-filled for point >= 0xA0 */
	uint32_t indexblock_off;
	uint16_t secsize;	// bytes
	uint8_t _idk12;
	char _idk13[0x11];
	uint32_t sec_first;
	uint64_t sec_off;	// bytes from beginning of .mdf file
	uint32_t filenames_num;
	uint32_t filenames_off;
	char _idk38[0x18];
} __attribute__((packed));

void track_ntoh(struct track_s *track)
{
	track->indexblock_off = le32toh(track->indexblock_off);
	track->secsize = le16toh(track->secsize);
	track->sec_first = le32toh(track->sec_first);
	track->sec_off = le64toh(track->sec_off);
	track->filenames_num = le32toh(track->filenames_num);
	track->filenames_off = le32toh(track->filenames_off);
}

struct index_s {
	uint32_t block_first;
	uint32_t block_last;
} __attribute__((packed));

void index_ntoh(struct index_s *index)
{
	index->block_first = le32toh(index->block_first);
	index->block_last = le32toh(index->block_last);
}

uint8_t xchg4(uint8_t a)
{
	unsigned lsb = a & 0x0f;
	unsigned msb = (a & 0xf0) >> 4;
	return (lsb << 4) | msb;
}

struct track_s *tracks = NULL;
unsigned numtracks = 0;

int GetTrackForPoint(unsigned point)
{
	if (!tracks) return -1;

	for (unsigned i = 0; i < numtracks; i++) {
		if (tracks[i].pointno == point) return i;
	}

	return -1;
}

int main(int argc, char *argv[])
{
	int rc;
	char *infilename = NULL;
	char *outfilename = NULL;
	bool verbose = false;

	struct trackmode_info_s ti = {0};

	if (sizeof(struct mds_s) != 0x58)
		errx(1, "bad size of struct mds_s");
	
	if (sizeof(struct session_s) != 0x18)
		errx(1, "bad size of struct session_s");
	
	if(sizeof(struct track_s) != 0x50)
		errx(1, "bad size of struct track_s");
	
	while ((rc = getopt(argc, argv, "i:o:vV")) != -1)
		switch (rc) {
		case 'i':
			if (infilename)
				usage();
			infilename = optarg;
			break;
		case 'o':
			if (outfilename)
				usage();
			outfilename = optarg;
			break;
		case 'v':
			verbose = true;
			break;
		case 'V':
			fprintf(stderr, "%s\n", PROG_VERSION);
			exit(EXIT_SUCCESS);
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (not infilename)
		usage();
	if (*argv != NULL)
		usage();
	
	struct MappedFile_s mds_file;
	mds_file = MappedFile_Open(infilename, false);
	if (!mds_file.data) err(1, "couldn't open '%s' for reading", infilename);

	//
	// Open up MDS header.
	//

	struct mds_s mds;
	memcpy(&mds, mds_file.data, sizeof(mds));
	if (!memcmp(mds.magic, "MEDIA_DESCRIPTOR", sizeof(mds.magic)))
		errx(1, "not an mds file? bad magic in '%s'", infilename);
	mds_ntoh(&mds);

	if (verbose) {
		hexdump(&mds, sizeof(mds));
		printf("version: v%u.%u\n", mds.version[0], mds.version[1]);
		printf("media: %s\n", mds_mediatype_tostring(mds.mediatype));
		printf("sessions: %u\n", mds.numsessions);
		printf("disc off: %08x\n", mds.discstruct_off);
		printf("session off: %08x\n", mds.session_off);
		printf("dpm off: %08x\n", mds.dpm_off);
	}

	//
	// Open session header.
	//

	struct session_s session;
	memcpy(&session, mds_file.data + mds.session_off, sizeof(session));
	session_ntoh(&session);

	if (verbose) {
		hexdump(&session, sizeof(session));
		printf("sec_first: %d\n", (int32_t)session.sec_first);
		printf("sec_last: %d\n", (int32_t)session.sec_last);
		printf("numsession: %u\n", session.numsession);
		printf("numtracks: %u\n", session.numtracks);
		printf("numtracks2: %u\n", session.numtracks2);
		printf("track_first: %u\n", session.track_first);
		printf("track_last: %u\n", session.track_last);
		printf("track_off: %08x\n", session.track_off);
	}

	//
	// Set up track structs.
	//

	numtracks = session.numtracks;
	tracks = calloc(sizeof(struct track_s), numtracks);
	if (!tracks) err(1, "in calloc");

	for (unsigned tracknum = 0; tracknum < numtracks; tracknum++) {
		memcpy(&tracks[tracknum], mds_file.data + session.track_off + tracknum*sizeof(struct track_s), sizeof(struct track_s));
		track_ntoh(&tracks[tracknum]);
	}

	if (verbose) for (unsigned tracknum = 0; tracknum < numtracks; tracknum++) {
		struct track_s track = tracks[tracknum];

		printf("track block %2u:\n", tracknum);
		printf("\tpointno: %02Xh\n", track.pointno);

		switch (track.pointno) {
		case 0xA0:
			printf("\tfirst track no: %u\n", track.minute);
			printf("\tdisk type: %02xh ", track.second);
			switch (track.second) {
			case 0x00:	printf("(CD-DA or CD-ROM)\n");	break;
			case 0x10:	printf("(CD-I)\n");		break;
			case 0x20:	printf("(CD-ROM XA)\n");	break;
			default:	printf("(unknown)\n");		break;
			}
			break;
		case 0xA1:
			printf("\tlast track no: %u\n", track.minute);
			break;
		case 0xA2:
			printf("\tend of disc msf: %02u:%02u:%02u\n",
				track.minute,
				track.second,
				track.frame
			);
			break;
		default:
			printf("\ttrackmode: %s\n", mds_trackmode_tostring(track.trackmode));
			printf("\tnumsubchannels: %u\n", track.numsubchannels);
			printf("\tadr: %02Xh\n", xchg4(track.adr));
			printf("\ttrackno: %u\n", track.trackno);
			printf("\tmsf: %02u:%02u:%02u\n", track.minute, track.second, track.frame);
			printf("\tindexblock_off: %08x\n", track.indexblock_off);
			printf("\tsecsize: %xh\n", track.secsize);
			printf("\tsec_first: %u\n", track.sec_first);
			printf("\tsec_off: %016lx\n", track.sec_off);
			printf("\tfilenames_num: %u\n", track.filenames_num);
			printf("\tfilenames_off: %08x\n", track.filenames_off);
			break;
		}
	}

	//
	// Find the first data track.
	//

	int datatrack = -1;
	for (int i = 0; i < numtracks; i++) {
		if ((tracks[i].trackmode >= TM_MODE1) || (tracks[i].trackmode == TM_DVD)) {
			datatrack = i;
			break;
		}
	}

	if (datatrack == -1) {
		errx(1, "no data track found");
	}

	unsigned numblocks = 0;
	if (datatrack < (numtracks - 1)) {
		numblocks = tracks[datatrack + 1].sec_first - tracks[datatrack].sec_first;
	} else {
		numblocks = session.sec_last;
	}
	printf("%u\n", datatrack);
	printf("%u\n", numblocks);
	printf("(%u %u)\n", tracks[datatrack+1].sec_first, tracks[datatrack].sec_first);

	ti = get_info_for_trackmode(tracks[datatrack].trackmode);
	if (ti.last) errx(1, "unknown track mode '%02Xh'", tracks[datatrack].trackmode);
	ti.data_stride = tracks[datatrack].secsize;

	if (verbose) {
		printf("\n");
		printf("trackinfo:\n");
		printf("data_stride: %xh\n", ti.data_stride);
		printf("data_off: %xh\n", ti.data_off);
		printf("data_len: %xh\n", ti.data_len);
	}

	MappedFile_Close(mds_file);
	mds_file.data = NULL;

	if (not outfilename) {
		if (verbose == 0) {
			usage();
		} else {
			return EXIT_SUCCESS;
		}
	}

	char *mdfname = strdup(infilename);
	if (!mdfname) err(1, "in strdup");
	if (mdfname[strlen(mdfname)-1] != 's')
		errx(1, "input file name does not end with 's'");
	mdfname[strlen(mdfname)-1] = 'f';

	struct MappedFile_s mdf_file = MappedFile_Open(mdfname, false);
	if (!mdf_file.data)
		err(1, "couldn't open '%s' for reading", mdfname);
	free(mdfname);

	FILE *out = fopen(outfilename, "w");
	if (!out) err(1, "couldn't open file for writing");
	if (ti.data_len == ti.data_stride) {
		// write the whole file in one go, if we can.
		fwrite(mdf_file.data, numblocks, ti.data_len, out);
	} else {
		for (size_t block = 0; block < numblocks; block++) {
			size_t sRc;
			sRc = fwrite(mdf_file.data + (size_t)ti.data_stride*block + (size_t)ti.data_off, (size_t)ti.data_len, 1, out);
			if (sRc != 1) err(1, "in fwrite");
		}
	}
	rc = fclose(out);
	if (rc) err(1, "couldn't close file");
	out = NULL;

	MappedFile_Close(mdf_file);
	mdf_file.data = NULL;

	return EXIT_SUCCESS;
}

static void noreturn usage(void)
{
	(void)fprintf(stderr, "usage: %s [-v] -i <mdsfile> -o <isofile>\n",
		__progname
	);
	exit(EXIT_FAILURE);
}
