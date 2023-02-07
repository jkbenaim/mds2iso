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
	{ TM_AUDIO, 0x930, 0 },
	{ TM_MODE1, 0x800, 0x10 },
	{ TM_MODE2, 0x920, 0x10 },
	{ TM_MODE2_FORM1, 0x800, 0x18 },
	{ TM_MODE2_FORM2, 0x914, 0x18 },
	{ TM_MODE2_SUB, 0x990, 0 /* ??? not sure about this one */},
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
	case TM_AUDIO: return "AUDIO";
	case TM_MODE1: return "MODE1";
	case TM_MODE2: return "MODE2";
	case TM_MODE2_FORM1: return "MODE2_FORM1";
	case TM_MODE2_FORM2: return "MODE2_FORM2";
	case TM_MODE2_SUB: return "MODE2 (with subchannels)";
	default: return "(unknown)";
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
	uint8_t numdatablocks;
	uint8_t numdatablocks2;
	uint16_t track_first;
	uint16_t track_last;
	uint32_t _idk10;
	uint32_t datablock_off;
} __attribute__((packed));

void session_ntoh(struct session_s *session)
{
	session->sec_first = le32toh(session->sec_first);
	session->sec_last = le32toh(session->sec_last);
	session->numsession = le16toh(session->numsession);
	session->track_first = le16toh(session->track_first);
	session->track_last = le16toh(session->track_last);
	session->datablock_off = le32toh(session->datablock_off);
}

struct datablock_s {
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
	uint64_t sec_off;	// bytes from begin of .mdf file
	uint32_t filenames_num;
	uint32_t filenames_off;
	char _idk38[0x18];
} __attribute__((packed));

void datablock_ntoh(struct datablock_s *datablock)
{
	datablock->indexblock_off = le32toh(datablock->indexblock_off);
	datablock->secsize = le16toh(datablock->secsize);
	datablock->sec_first = le32toh(datablock->sec_first);
	datablock->sec_off = le64toh(datablock->sec_off);
	datablock->filenames_num = le32toh(datablock->filenames_num);
	datablock->filenames_off = le32toh(datablock->filenames_off);
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
	
	if(sizeof(struct datablock_s) != 0x50)
		errx(1, "bad size of struct datablock_s");
	
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
	if (not outfilename)
		usage();
	if (*argv != NULL)
		usage();
	
	struct MappedFile_s mds_file;
	mds_file = MappedFile_Open(infilename, false);
	if (!mds_file.data) err(1, "couldn't open '%s' for reading", infilename);

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

	struct session_s session;
	memcpy(&session, mds_file.data + mds.session_off, sizeof(session));
	session_ntoh(&session);

	if (verbose) {
		hexdump(&session, sizeof(session));
		printf("sec_first: %d\n", (int32_t)session.sec_first);
		printf("sec_last: %d\n", (int32_t)session.sec_last);
		printf("numsession: %u\n", session.numsession);
		printf("numdatablocks: %u\n", session.numdatablocks);
		printf("numdatablocks2: %u\n", session.numdatablocks2);
		printf("track_first: %u\n", session.track_first);
		printf("track_last: %u\n", session.track_last);
		printf("datablock_off: %08x\n", session.datablock_off);
	}

	for (unsigned blocknum = 0; blocknum < session.numdatablocks; blocknum++) {
		struct datablock_s datablock;
		memcpy(&datablock, mds_file.data + session.datablock_off + blocknum*sizeof(datablock), sizeof(datablock));
		datablock_ntoh(&datablock);

		if (verbose) {
			printf("data block %u\n", blocknum);
			hexdump(&datablock, sizeof(datablock));
			printf("\ttrackmode: %s\n", mds_trackmode_tostring(datablock.trackmode));
			printf("\tnumsubchannels: %u\n", datablock.numsubchannels);
			printf("\tadr: %02Xh\n", datablock.adr);
			printf("\ttrackno: %u\n", datablock.trackno);
			printf("\tpointno: %02Xh\n", datablock.pointno);
			printf("\tmsf: %u:%u:%u\n", datablock.minute, datablock.second, datablock.frame);
			printf("\tindexblock_off: %08x\n", datablock.indexblock_off);
			printf("\tsecsize: %xh\n", datablock.secsize);
			printf("\tsec_first: %u\n", datablock.sec_first);
			printf("\tsec_off: %016lx\n", datablock.sec_off);
			printf("\tfilenames_num: %u\n", datablock.filenames_num);
			printf("\tfilenames_off: %08x\n", datablock.filenames_off);
		}

		ti = get_info_for_trackmode(datablock.trackmode);
		if (ti.last) errx(1, "unknown track mode '%02Xh'", datablock.trackmode);

		if (verbose) {
			printf("\tdata_len: %u\n", ti.data_len);
			printf("\tdata_off: %u\n", ti.data_off);
		}

		ti.data_stride = datablock.secsize;
	}

	if (verbose) {
		printf("\n");
		printf("trackinfo:\n");
		printf("data_stride: %xh\n", ti.data_stride);
		printf("data_off: %xh\n", ti.data_off);
		printf("data_len: %xh\n", ti.data_len);
	}

	MappedFile_Close(mds_file);
	mds_file.data = NULL;


	char *mdfname = strdup(infilename);
	if (!mdfname) err(1, "in strdup");
	if (mdfname[strlen(mdfname)-1] != 's')
		errx(1, "input file name does not end with 's'");
	mdfname[strlen(mdfname)-1] = 'f';

	struct MappedFile_s mdf_file = MappedFile_Open(mdfname, false);
	if (!mdf_file.data)
		err(1, "couldn't open '%s' for reading", mdfname);
	free(mdfname);

	size_t checksize = (size_t)ti.data_stride * (size_t)session.sec_last;
	if (mdf_file.size != checksize)
		errx(1, "mdf size of %lu is different from expected %lu", mdf_file.size, checksize);
	
	FILE *out = fopen(outfilename, "w");
	if (!out) err(1, "couldn't open file for writing");
	for (unsigned block = 0; block < session.sec_last; block++) {
		size_t sRc;
		sRc = fwrite(mdf_file.data + ti.data_stride*block + ti.data_off, ti.data_len, 1, out);
		if (sRc != 1) err(1, "in fwrite");
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
