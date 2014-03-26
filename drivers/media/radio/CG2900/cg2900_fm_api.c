/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Linux FM Host API's for ST-Ericsson FM Chip.
 *
 * Author: Hemant Gupta <hemant.gupta@stericsson.com> for ST-Ericsson.
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include "cg2900.h"
#include "cg2900_fm_driver.h"

#define CG2910_FM_LUT_INFO_FILE "FM_FW_CG2910_1_0_P1_4_lut_info.fw"
#define CG2910_FM_PROG_INFO_FILE "FM_FW_CG2910_1_0_P1_4_prog_info.fw"
#define CG2910_LUT_IDX		0
#define CG2910_PROG_IDX		1
#define CG2910_MAX_FILES_DL	2

#define CG2900_FM_BT_SRC_COEFF_INFO_FILE "cg2900_fm_bt_src_coeff_info.fw"
#define CG2900_FM_EXT_SRC_COEFF_INFO_FILE "cg2900_fm_ext_src_coeff_info.fw"
#define CG2900_FM_FM_COEFF_INFO_FILE	"cg2900_fm_fm_coeff_info.fw"
#define CG2900_FM_FM_PROG_INFO_FILE	"cg2900_fm_fm_prog_info.fw"
#define CG2900_FM_LINE_BUFFER_LENGTH	128
#define CG2900_FM_FILENAME_MAX		128
#define FW_FILE_PARAM_LEN				3
/* RDS Tx PTY set to Other music */
#define OTHER_MUSIC			15
#define DEFAULT_AUDIO_DEVIATION 0x1AA9
#define DEFAULT_NOTIFICATION_HOLD_OFF_TIME 0x000A

static bool fm_rds_status;
static bool fm_prev_rds_status;
static u16 program_identification_code;
static u16 default_program_identification_code = 0x1234;
static u16 program_type_code;
static u16 default_program_type_code = OTHER_MUSIC;
static char program_service[MAX_PSN_SIZE];
static char default_program_service[MAX_PSN_SIZE] = "FM-Xmit ";
static char radio_text[MAX_RT_SIZE];
static char default_radio_text[MAX_RT_SIZE] = "Default Radio Text "
    "Default Radio Text Default Radio Text Default";
static bool a_b_flag;
u8 fm_event;
static struct mutex rds_mutex;
struct cg2900_fm_rds_buf fm_rds_buf[MAX_RDS_BUFFER][MAX_RDS_GROUPS];
struct cg2900_fm_rds_info fm_rds_info;
static enum cg2900_fm_state fm_state;
static enum cg2900_fm_mode fm_mode;
struct cg2900_version_info version_info;

/**
 * cg2900_fm_get_one_line_of_text()- Get One line of text from a file.
 *
 * Replacement function for stdio function fgets.This function extracts one
 * line of text from input file.
 *
 * @wr_buffer: Buffer to copy text to.
 * @max_nbr_of_bytes: Max number of bytes to read, i.e. size of rd_buffer.
 * @rd_buffer: Data to parse.
 * @bytes_copied: Number of bytes copied to wr_buffer.
 *
 * Returns:
 *   Pointer to next data to read.
 */
static char *cg2900_fm_get_one_line_of_text(
			char *wr_buffer,
			int max_nbr_of_bytes,
			char *rd_buffer,
			int *bytes_copied
			)
{
	char *curr_wr = wr_buffer;
	char *curr_rd = rd_buffer;
	char in_byte;

	*bytes_copied = 0;

	do {
		*curr_wr = *curr_rd;
		in_byte = *curr_wr;
		curr_wr++;
		curr_rd++;
		(*bytes_copied)++;
	} while ((*bytes_copied <= max_nbr_of_bytes) && (in_byte != '\0')
		 && (in_byte != '\n'));
	*curr_wr = '\0';
	return curr_rd;
}

/**
 * cg2900_fm_get_file_to_load() - Parse info file and find correct target file.
 *
 * @fw:	Firmware structure containing file data.
 * @file_name: (out) Pointer to name of requested file.
 *
 * Returns:
 *   True,  if target file was found,
 *   False, otherwise.
 */
static bool cg2900_fm_get_file_to_load(
			const struct firmware *fw,
			char **file_name
			)
{
	char *line_buffer;
	char *curr_file_buffer;
	int bytes_left_to_parse = fw->size;
	int bytes_read = 0;
	bool file_found = false;

	curr_file_buffer = (char *)&(fw->data[0]);

	line_buffer = kmalloc(CG2900_FM_LINE_BUFFER_LENGTH,
				GFP_KERNEL);

	if (line_buffer == NULL) {
		FM_ERR_REPORT("Failed to allocate:"
			      "file_name 0x%X, line_buffer 0x%X",
			      (unsigned int)file_name,
			      (unsigned int)line_buffer);
		goto error;
	}

	while (!file_found) {
		/* Get one line of text from the file to parse */
		curr_file_buffer =
		    cg2900_fm_get_one_line_of_text(line_buffer,
					min
					(CG2900_FM_LINE_BUFFER_LENGTH,
					 (int)(fw->size -
						bytes_read)),
						curr_file_buffer,
						&bytes_read);

		bytes_left_to_parse -= bytes_read;
		if (bytes_left_to_parse <= 0) {
			/* End of file => Leave while loop */
			FM_ERR_REPORT("Reached end of file."
				      "No file found!");
			break;
		}

		/*
		 * Check if the line of text is a comment
		 * or not, comments begin with '#'
		 */
		if (*line_buffer != '#') {
			u32 hci_rev = 0;
			u32 lmp_sub = 0;

			FM_DEBUG_REPORT("Found a valid line <%s>",
					line_buffer);

			/*
			 * Check if we can find the correct
			 * HCI revision and LMP subversion
			 * as well as a file name in the text line
			 * Store the filename if the actual file can
			 * be found in the file system
			 */
			if (sscanf(line_buffer, "%x%x%s",
				   (unsigned int *)&hci_rev,
				   (unsigned int *)&lmp_sub,
				   *file_name) == FW_FILE_PARAM_LEN
			    && hci_rev == version_info.revision
			    && lmp_sub == version_info.sub_version) {
				FM_INFO_REPORT("File name = %s "
					       "HCI Revision"
					       "= 0x%04X LMP "
					       "Subversion = 0x%04X",
					       *file_name,
					       (unsigned int)hci_rev,
					       (unsigned int)lmp_sub);

				/*
				 * Name has already been stored above.
				 * Nothing more to do
				 */
				file_found = true;
			} else {
		/*Zero the name buffer so  it is clear to next read*/
				memset(*file_name, 0x00,
				       CG2900_FM_FILENAME_MAX);
			}
		}
	}
	kfree(line_buffer);
error:
	return file_found;
}


/**
 * cg2910_fm_load_firmware() - Loads the FM lut and
 * Program firmware files for CG2910
 *
 * @device: Pointer to char device requesting the operation.
 *
 * Returns:
 *  0, if firmware download is successful
 *  -ENOENT, file not found.
 *  -ENOMEM, out of memory
 */
static int cg2910_fm_load_firmware(
			struct device *device
			)
{
	int err;
	bool file_found;
	int result = 0;
	const struct firmware *fm_fw_info[2];
	const struct firmware *fm_firmware[2];
	char *fm_fw_file_name = NULL;
	int loopi = 0;

	FM_INFO_REPORT("+cg2910_fm_load_firmware");
	fm_fw_info[CG2910_LUT_IDX] = NULL;
	fm_fw_info[CG2910_PROG_IDX] = NULL;
	fm_firmware[CG2910_LUT_IDX] = NULL;
	fm_firmware[CG2910_PROG_IDX] = NULL;

	/* Open fm_fw_info lut file. */
	err = request_firmware(&fm_fw_info[CG2910_LUT_IDX],
			CG2910_FM_LUT_INFO_FILE, device);
	if (err) {
		FM_ERR_REPORT("cg2910_fm_load_firmware: "
				"Couldn't get fm_fw_info lut file");
		result = -ENOENT;
		goto error;
	}

	/* Open fm_fw_info prog file. */
	err = request_firmware(&fm_fw_info[CG2910_PROG_IDX],
			CG2910_FM_PROG_INFO_FILE, device);
	if (err) {
		FM_ERR_REPORT("cg2910_fm_load_firmware: "
				"Couldn't get fm_fw_info prog file");
		result = -ENOENT;
		goto error;
	}

	fm_fw_file_name = kmalloc(CG2900_FM_FILENAME_MAX,
			GFP_KERNEL);
	if (fm_fw_file_name == NULL) {
		FM_ERR_REPORT("cg2910_fm_load_firmware: "
				"Couldn't allocate memory for "
				"fm_fw_file_name");
		result = -ENOMEM;
		goto error;
	}

	/* Put a loop for downloading lut and prog */
	for (loopi = 0; loopi < CG2910_MAX_FILES_DL; loopi++) {
		/*
		 * Now we have the fm_fw_info file. See if we can
		 * find the right fm_fw_file_name file as well
		 */
		file_found = cg2900_fm_get_file_to_load(fm_fw_info[loopi],
				&fm_fw_file_name);

		if (!file_found) {
			FM_ERR_REPORT("cg2910_fm_load_firmware: "
					"Couldn't find fm_fw_file_name file!! "
					"Major error!!!");
			result = -ENOENT;
			goto error;
		}

		/*
		 * OK. Now it is time to download the firmware
		 * First download lut file & then prog
		 */
		err = request_firmware(&fm_firmware[loopi],
				fm_fw_file_name, device);
		if (err < 0) {
			FM_ERR_REPORT("cg2910_fm_load_firmware: "
					"Couldn't get fm_firmware"
					" file, err = %d", err);
			result = -ENOENT;
			goto error;
		}

		FM_INFO_REPORT("cg2910_fm_load_firmware: "
				"Downloading %s of %d bytes",
				fm_fw_file_name, fm_firmware[loopi]->size);
		if (fmd_send_fm_firmware((u8 *) fm_firmware[loopi]->data,
				fm_firmware[loopi]->size)) {
			FM_ERR_REPORT("cg2910_fm_load_firmware: Error in "
					"downloading %s", fm_fw_file_name);
			result = -ENOENT;
			goto error;
		}
	}

error:
	/* Release fm_fw_info lut and prog file */
	if (fm_fw_info[CG2910_LUT_IDX])
		release_firmware(fm_fw_info[CG2910_LUT_IDX]);
	if (fm_fw_info[CG2910_PROG_IDX])
		release_firmware(fm_fw_info[CG2910_PROG_IDX]);

	if (fm_firmware[CG2910_LUT_IDX])
		release_firmware(fm_firmware[CG2910_LUT_IDX]);
	if (fm_firmware[CG2910_PROG_IDX])
		release_firmware(fm_firmware[CG2910_PROG_IDX]);

	/* Free Allocated memory */
	kfree(fm_fw_file_name);
	FM_DEBUG_REPORT("-cg2910_fm_load_firmware: returning %d",
			result);
	return result;
}

/**
 * cg2900_fm_load_firmware() - Loads the FM Coeffecients and F/W file(s)
 * for CG2900
 * @device: Pointer to char device requesting the operation.
 *
 * Returns:
 *  0, if firmware download is successful
 *  -ENOENT, file not found.
 *  -ENOMEM, out of memory
 */
static int cg2900_fm_load_firmware(
			struct device *device
			)
{
	int err;
	bool file_found;
	int result = 0;
	const struct firmware *bt_src_coeff_info;
	const struct firmware *ext_src_coeff_info;
	const struct firmware *fm_coeff_info;
	const struct firmware *fm_prog_info;
	char *bt_src_coeff_file_name = NULL;
	char *ext_src_coeff_file_name = NULL;
	char *fm_coeff_file_name = NULL;
	char *fm_prog_file_name = NULL;

	FM_INFO_REPORT("+cg2900_fm_load_firmware");

	/* Open bt_src_coeff info file. */
	err = request_firmware(&bt_src_coeff_info,
			       CG2900_FM_BT_SRC_COEFF_INFO_FILE, device);
	if (err) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get bt_src_coeff info file");
		result = -ENOENT;
		goto error;
	}

	/*
	 * Now we have the bt_src_coeff info file.
	 * See if we can find the right bt_src_coeff file as well
	 */
	bt_src_coeff_file_name = kmalloc(CG2900_FM_FILENAME_MAX,
				GFP_KERNEL);
	if (bt_src_coeff_file_name == NULL) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't allocate memory for "
			      "bt_src_coeff_file_name");
		release_firmware(bt_src_coeff_info);
		result = -ENOMEM;
		goto error;
	}
	file_found = cg2900_fm_get_file_to_load(bt_src_coeff_info,
					     &bt_src_coeff_file_name);

	/* Now we are finished with the bt_src_coeff info file */
	release_firmware(bt_src_coeff_info);

	if (!file_found) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't find bt_src_coeff file!! "
			      "Major error!!!");
		result = -ENOENT;
		goto error;
	}

	/* Open ext_src_coeff info file. */
	err = request_firmware(&ext_src_coeff_info,
			       CG2900_FM_EXT_SRC_COEFF_INFO_FILE, device);
	if (err) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get ext_src_coeff_info info file");
		result = -ENOENT;
		goto error;
	}

	/*
	 * Now we have the ext_src_coeff info file. See if we can
	 * find the right ext_src_coeff file as well
	 */
	ext_src_coeff_file_name = kmalloc(CG2900_FM_FILENAME_MAX,
				GFP_KERNEL);
	if (ext_src_coeff_file_name == NULL) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't allocate memory for "
			      "ext_src_coeff_file_name");
		release_firmware(ext_src_coeff_info);
		result = -ENOMEM;
		goto error;
	}
	file_found = cg2900_fm_get_file_to_load(ext_src_coeff_info,
					     &ext_src_coeff_file_name);

	/* Now we are finished with the ext_src_coeff info file */
	release_firmware(ext_src_coeff_info);

	if (!file_found) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't find ext_src_coeff_info "
			      "file!!! Major error!");
		result = -ENOENT;
		goto error;
	}

	/* Open fm_coeff info file. */
	err = request_firmware(&fm_coeff_info,
			       CG2900_FM_FM_COEFF_INFO_FILE, device);
	if (err) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get fm_coeff info file");
		result = -ENOENT;
		goto error;
	}

	/*
	 * Now we have the fm_coeff_info info file.
	 * See if we can find the right fm_coeff_info file as well
	 */
	fm_coeff_file_name = kmalloc(CG2900_FM_FILENAME_MAX,
				GFP_KERNEL);
	if (fm_coeff_file_name == NULL) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
						"Couldn't allocate memory for "
						"fm_coeff_file_name");
		release_firmware(fm_coeff_info);
		result = -ENOMEM;
		goto error;
	}
	file_found = cg2900_fm_get_file_to_load(fm_coeff_info,
					     &fm_coeff_file_name);

	/* Now we are finished with the fm_coeff info file */
	release_firmware(fm_coeff_info);

	if (!file_found) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't find fm_coeff file!!! "
			      "Major error!");
		result = -ENOENT;
		goto error;
	}

	/* Open fm_prog info file. */
	err = request_firmware(&fm_prog_info,
				CG2900_FM_FM_PROG_INFO_FILE, device);
	if (err) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get fm_prog_info info file");
		result = -ENOENT;
		goto error;
	}

	/*
	 * Now we have the fm_prog info file.
	 * See if we can find the right fm_prog file as well
	 */
	fm_prog_file_name = kmalloc(CG2900_FM_FILENAME_MAX,
				GFP_KERNEL);
	if (fm_prog_file_name == NULL) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
						"Couldn't allocate memory for "
						"fm_prog_file_name");
		release_firmware(fm_prog_info);
		result = -ENOMEM;
		goto error;
	}
	file_found = cg2900_fm_get_file_to_load(fm_prog_info,
					     &fm_prog_file_name);

	/* Now we are finished with fm_prog patch info file */
	release_firmware(fm_prog_info);

	if (!file_found) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't find fm_prog_info file!!! "
			      "Major error!");
		result = -ENOENT;
		goto error;
	}

	/* OK. Now it is time to download the firmware */
	err = request_firmware(&bt_src_coeff_info,
			       bt_src_coeff_file_name, device);
	if (err < 0) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get bt_src_coeff file, err = %d", err);
		result = -ENOENT;
		goto error;
	}

	FM_INFO_REPORT("cg2900_fm_load_firmware: Downloading %s of %d bytes",
		       bt_src_coeff_file_name, bt_src_coeff_info->size);
	if (fmd_send_fm_firmware((u8 *) bt_src_coeff_info->data,
				 bt_src_coeff_info->size)) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: Error in "
				"downloading %s", bt_src_coeff_file_name);
		release_firmware(bt_src_coeff_info);
		result = -ENOENT;
		goto error;
	}

	/* Now we are finished with the bt_src_coeff info file */
	release_firmware(bt_src_coeff_info);
	err = request_firmware(&ext_src_coeff_info,
			       ext_src_coeff_file_name, device);
	if (err < 0) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get ext_src_coeff file, err = %d", err);
		result = -ENOENT;
		goto error;
	}

	FM_INFO_REPORT("cg2900_fm_load_firmware: Downloading %s of %d bytes",
		       ext_src_coeff_file_name, ext_src_coeff_info->size);
	if (fmd_send_fm_firmware((u8 *) ext_src_coeff_info->data,
				 ext_src_coeff_info->size)) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: Error in "
				"downloading %s", ext_src_coeff_file_name);
		release_firmware(ext_src_coeff_info);
		result = -ENOENT;
		goto error;
	}

	/* Now we are finished with the bt_src_coeff info file */
	release_firmware(ext_src_coeff_info);

	err = request_firmware(&fm_coeff_info, fm_coeff_file_name, device);
	if (err < 0) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get fm_coeff file, err = %d", err);
		result = -ENOENT;
		goto error;
	}

	FM_INFO_REPORT("cg2900_fm_load_firmware: Downloading %s of %d bytes",
		       fm_coeff_file_name, fm_coeff_info->size);
	if (fmd_send_fm_firmware((u8 *) fm_coeff_info->data,
				 fm_coeff_info->size)) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: Error in "
				"downloading %s", fm_coeff_file_name);
		release_firmware(fm_coeff_info);
		result = -ENOENT;
		goto error;
	}

	/* Now we are finished with the bt_src_coeff info file */
	release_firmware(fm_coeff_info);

	err = request_firmware(&fm_prog_info, fm_prog_file_name, device);
	if (err < 0) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: "
			      "Couldn't get fm_prog file, err = %d", err);
		result = -ENOENT;
		goto error;
	}

	FM_INFO_REPORT("cg2900_fm_load_firmware: Downloading %s of %d bytes",
		       fm_prog_file_name, fm_prog_info->size);
	if (fmd_send_fm_firmware((u8 *) fm_prog_info->data,
		fm_prog_info->size)) {
		FM_ERR_REPORT("cg2900_fm_load_firmware: Error in "
				"downloading %s", fm_prog_file_name);
		release_firmware(fm_prog_info);
		result = -ENOENT;
		goto error;
	}

	/* Now we are finished with the bt_src_coeff info file */
	release_firmware(fm_prog_info);

error:
	/* Free Allocated memory */
	if (bt_src_coeff_file_name != NULL)
		kfree(bt_src_coeff_file_name);
	if (ext_src_coeff_file_name != NULL)
		kfree(ext_src_coeff_file_name);
	if (fm_coeff_file_name != NULL)
		kfree(fm_coeff_file_name);
	if (fm_prog_file_name != NULL)
		kfree(fm_prog_file_name);
	FM_DEBUG_REPORT("-cg2900_fm_load_firmware: returning %d",
			result);
	return result;
}

/**
 * cg2900_fm_transmit_rds_groups()- Transmits the RDS Groups.
 *
 * Stores the RDS Groups in Chip's buffer and each group is
 * transmitted every 87.6 ms.
 *
 * Returns:
 *	 0,  if operation completed successfully.
 *	 -EINVAL, otherwise
 */
static int cg2900_fm_transmit_rds_groups(void)
{
	int result = 0;
	u16 group_position = 0;
	u8 block1[2];
	u8 block2[2];
	u8 block3[2];
	u8 block4[2];
	int index1 = 0;
	int index2 = 0;
	int group_0B_count = 0;
	int group_2A_count = 0;

	FM_INFO_REPORT("cg2900_fm_transmit_rds_groups");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_transmit_rds_groups: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	while (group_position < 20 && result == 0) {
		if (group_position < 4) {
			/* Transmit PSN in Group 0B */
			block1[0] = program_identification_code;
			block1[1] = program_identification_code >> 8;
			/* M/S bit set to Music */
			if (group_0B_count % 4 == 0) {
				/* Manipulate DI bit */
				block2[0] =
				    (0x08 | ((program_type_code & 0x07)
					     << 5))
				    + group_0B_count;
			} else {
				block2[0] =
				    (0x0C | ((program_type_code & 0x07)
					     << 5))
				    + group_0B_count;
			}
			block2[1] =
			    0x08 | ((program_type_code & 0x18) >> 3);
			block3[0] = program_identification_code;
			block3[1] = program_identification_code >> 8;
			block4[0] = program_service[index1 + 1];
			block4[1] = program_service[index1 + 0];
			index1 += 2;
			group_0B_count++;
		} else {
			/* Transmit RT in Group 2A */
			block1[0] = program_identification_code;
			block1[1] = program_identification_code >> 8;
			if (a_b_flag)
				block2[0] = (0x10 |
					     ((program_type_code & 0x07)
					      << 5)) + group_2A_count;
			else
				block2[0] = (0x00 |
					     ((program_type_code & 0x07)
					      << 5)) + group_2A_count;
			block2[1] = 0x20 | ((program_type_code & 0x18)
					    >> 3);
			block3[0] = radio_text[index2 + 1];
			block3[1] = radio_text[index2 + 0];
			block4[0] = radio_text[index2 + 3];
			block4[1] = radio_text[index2 + 2];
			index2 += 4;
			group_2A_count++;
		}
		FM_DEBUG_REPORT("%02x%02x "
				"%02x%02x "
				"%02x%02x "
				"%02x%02x ",
				block1[1], block1[0],
				block2[1], block2[0],
				block3[1], block3[0],
				block4[1], block4[0]);
		result = fmd_tx_set_group(
				group_position,
				block1,
				block2,
				block3,
				block4);
		group_position++;
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_transmit_rds_groups: "
				      "fmd_tx_set_group failed %d",
				      (unsigned int)result);
			result = -EINVAL;
			break;
		}
	}
	a_b_flag = !a_b_flag;

error:
	FM_DEBUG_REPORT("cg2900_fm_transmit_rds_groups: returning %d",
			result);
	return result;
}

/**
 * cg2900_fm_check_rds_status()- Checks whether RDS was On previously
 *
 * This method is called on receiving interrupt for Seek Completion,
 * Scan completion and Block Scan completion. It will check whether RDS
 * was forcefully disabled before the above operations started and if the
 * previous RDS state was true, then RDS will be enabled back
 */
static void cg2900_fm_check_rds_status(void)
{
	FM_INFO_REPORT("cg2900_fm_check_rds_status");
	if (fm_prev_rds_status) {
		/* Restart RDS if it was active previously */
		cg2900_fm_rds_on();
		fm_prev_rds_status = false;
	}
}

/**
 * cg2900_fm_driver_callback()- Callback function indicating the event.
 *
 * This callback function is called on receiving irpt_CommandSucceeded,
 * irpt_CommandFailed, irpt_bufferFull, etc from FM chip.
 * @event: event for which the callback function was caled
 * from FM Driver.
 * @event_successful: Signifying whether the event is called from FM Driver
 * on receiving irpt_OperationSucceeded or irpt_OperationFailed.
 */
static void cg2900_fm_driver_callback(
			u8 event,
			bool event_successful
			)
{
	struct sk_buff *skb;

	FM_INFO_REPORT("cg2900_fm_driver_callback: "
			"event = %02x, event_successful = %x",
			event, event_successful);

	switch (event) {
	case FMD_EVENT_GEN_POWERUP:
		FM_DEBUG_REPORT("FMD_EVENT_GEN_POWERUP");
		break;
	case FMD_EVENT_ANTENNA_STATUS_CHANGED:
		FM_DEBUG_REPORT("FMD_EVENT_ANTENNA_STATUS_CHANGED");
		break;
	case FMD_EVENT_FREQUENCY_CHANGED:
		FM_DEBUG_REPORT("FMD_EVENT_FREQUENCY_CHANGED ");
		break;
	case FMD_EVENT_SEEK_STOPPED:
		FM_DEBUG_REPORT("FMD_EVENT_SEEK_STOPPED");
		skb = alloc_skb(SKB_FM_INTERRUPT_DATA,
			GFP_KERNEL);
		if (!skb) {
			FM_ERR_REPORT("cg2900_fm_driver_callback: "
					"Unable to Allocate Memory");
			return;
		}
		skb->data[0] = CG2900_EVENT_SCAN_CANCELLED;
		skb->data[1] = event_successful;
		skb_queue_tail(&fm_interrupt_queue, skb);
		wake_up_poll_queue();
		break;
	case FMD_EVENT_SEEK_COMPLETED:
		FM_DEBUG_REPORT("FMD_EVENT_SEEK_COMPLETED");
		cg2900_fm_check_rds_status();
		skb = alloc_skb(SKB_FM_INTERRUPT_DATA,
			GFP_KERNEL);
		if (!skb) {
			FM_ERR_REPORT("cg2900_fm_driver_callback: "
					"Unable to Allocate Memory");
			return;
		}
		skb->data[0] = CG2900_EVENT_SEARCH_CHANNEL_FOUND;
		skb->data[1] = event_successful;
		skb_queue_tail(&fm_interrupt_queue, skb);
		wake_up_poll_queue();
		break;
	case FMD_EVENT_SCAN_BAND_COMPLETED:
		FM_DEBUG_REPORT("FMD_EVENT_SCAN_BAND_COMPLETED");
		cg2900_fm_check_rds_status();
		skb = alloc_skb(SKB_FM_INTERRUPT_DATA,
			GFP_KERNEL);
		if (!skb) {
			FM_ERR_REPORT("cg2900_fm_driver_callback: "
					"Unable to Allocate Memory");
			return;
		}
		skb->data[0] = CG2900_EVENT_SCAN_CHANNELS_FOUND;
		skb->data[1] = event_successful;
		skb_queue_tail(&fm_interrupt_queue, skb);
		wake_up_poll_queue();
		break;
	case FMD_EVENT_BLOCK_SCAN_COMPLETED:
		FM_DEBUG_REPORT("FMD_EVENT_BLOCK_SCAN_COMPLETED");
		cg2900_fm_check_rds_status();
		skb = alloc_skb(SKB_FM_INTERRUPT_DATA,
			GFP_KERNEL);
		if (!skb) {
			FM_ERR_REPORT("cg2900_fm_driver_callback: "
					"Unable to Allocate Memory");
			return;
		}
		skb->data[0] = CG2900_EVENT_BLOCK_SCAN_CHANNELS_FOUND;
		skb->data[1] = event_successful;
		skb_queue_tail(&fm_interrupt_queue, skb);
		wake_up_poll_queue();
		break;
	case FMD_EVENT_AF_UPDATE_SWITCH_COMPLETE:
		FM_DEBUG_REPORT("FMD_EVENT_AF_UPDATE_SWITCH_COMPLETE");
		break;
	case FMD_EVENT_RDSGROUP_RCVD:
		FM_DEBUG_REPORT("FMD_EVENT_RDSGROUP_RCVD");
		/*
		* Release the rds semaphore, poll queue
		* will be woken-up in rds callback
		*/
		fmd_set_rds_sem();
		break;
	case FMD_EVENT_MONO_STEREO_TRANSITION_COMPLETE:
		FM_ERR_REPORT(
			"FMD_EVENT_MONO_STEREO_TRANSITION_COMPLETE");
		skb = alloc_skb(SKB_FM_INTERRUPT_DATA,
			GFP_KERNEL);
		if (!skb) {
			FM_ERR_REPORT("cg2900_fm_driver_callback: "
					"Unable to Allocate Memory");
			return;
		}
		skb->data[0] = CG2900_EVENT_MONO_STEREO_TRANSITION;
		skb->data[1] = event_successful;
		skb_queue_tail(&fm_interrupt_queue, skb);
		wake_up_poll_queue();
		break;
	default:
		FM_INFO_REPORT("cg2900_fm_driver_callback: "
			      "Unknown event = %x", event);
		break;
	}
}

/**
 * cg2900_fm_rds_callback()- Function to retrieve the RDS groups.
 *
 * This is called when the chip has received enough RDS groups
 * so an interrupt irpt_BufferFull is generated to read the groups.
 */
static void cg2900_fm_rds_callback(void)
{
	u8 index = 0;
	u16 rds_local_buf_count;
	int result;
	struct sk_buff *skb;

	FM_INFO_REPORT("cg2900_fm_rds_callback");

	/*
	 * Wait till interrupt is RDS Buffer
	 * full interrupt is received
	 */
	fmd_get_rds_sem();

	if (!fm_rds_status)
		return;

	/* RDS Data available, Read the Groups */
	mutex_lock(&rds_mutex);
	result = fmd_int_bufferfull(&rds_local_buf_count);

	if (0 != result)
		goto error;

	while (index < rds_local_buf_count) {
		/*
		 * Status are in reverse order because of Endianness
		 * of status byte received from chip
		 */
		result = fmd_rx_get_low_level_rds_groups(
			index,
			&fm_rds_buf[fm_rds_info.rds_head][index].block1,
			&fm_rds_buf[fm_rds_info.rds_head][index].block2,
			&fm_rds_buf[fm_rds_info.rds_head][index].block3,
			&fm_rds_buf[fm_rds_info.rds_head][index].block4,
			&fm_rds_buf[fm_rds_info.rds_head][index].status2,
			&fm_rds_buf[fm_rds_info.rds_head][index].status1,
			&fm_rds_buf[fm_rds_info.rds_head][index].status4,
			&fm_rds_buf[fm_rds_info.rds_head][index].status3);
		FM_INFO_REPORT("%04x %04x %04x %04x %02x %02x %02x %02x",
			fm_rds_buf[fm_rds_info.rds_head][index].block1,
			fm_rds_buf[fm_rds_info.rds_head][index].block2,
			fm_rds_buf[fm_rds_info.rds_head][index].block3,
			fm_rds_buf[fm_rds_info.rds_head][index].block4,
			fm_rds_buf[fm_rds_info.rds_head][index].status1,
			fm_rds_buf[fm_rds_info.rds_head][index].status2,
			fm_rds_buf[fm_rds_info.rds_head][index].status3,
			fm_rds_buf[fm_rds_info.rds_head][index].status4);

		if (0 != result)

			goto error;

		if (!fm_rds_status)
			return;

		index++;
	}
	fm_rds_info.rds_head++;
	if (fm_rds_info.rds_head == MAX_RDS_BUFFER)
		fm_rds_info.rds_head = 0;

	/* Queue the RDS event */
	skb = alloc_skb(SKB_FM_INTERRUPT_DATA,
		GFP_KERNEL);
	if (!skb) {
		FM_ERR_REPORT("cg2900_fm_rds_callback: "
				"Unable to Allocate Memory");
		goto error;
	}
	skb->data[0] = CG2900_EVENT_RDS_EVENT;
	skb->data[1] = true;
	skb_queue_tail(&fm_interrupt_queue, skb);

	/* Wake up the poll queue */
	wake_up_poll_queue();
error:
	mutex_unlock(&rds_mutex);
}

int cg2900_fm_init(void)
{
	int result = 0;

	FM_INFO_REPORT("cg2900_fm_init");

	if (CG2900_FM_STATE_DEINITIALIZED != fm_state) {
		FM_ERR_REPORT("cg2900_fm_init: Already Initialized");
		result = -EINVAL;
		goto error;
	}

	mutex_init(&rds_mutex);

	memset(&fm_rds_info, 0, sizeof(struct cg2900_fm_rds_info));
	memset(&version_info, 0, sizeof(struct cg2900_version_info));
	memset(
			fm_rds_buf,
			0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);

	/* Initalize the Driver */
	if (fmd_init() != 0) {
		result = -EINVAL;
		goto error;
	}

	/* Register the callback */
	if (fmd_register_callback(
		(fmd_radio_cb) cg2900_fm_driver_callback) != 0) {
		result = -EINVAL;
		goto error;
	}

	/* initialize global variables */
	fm_event = CG2900_EVENT_NO_EVENT;
	fm_state = CG2900_FM_STATE_INITIALIZED;
	fm_mode = CG2900_FM_IDLE_MODE;
	fm_prev_rds_status = false;

error:
	FM_DEBUG_REPORT("cg2900_fm_init: returning %d",
			result);
	return result;

}

int cg2900_fm_deinit(void)
{
	int result = 0;

	FM_INFO_REPORT("cg2900_fm_deinit");

	if (CG2900_FM_STATE_INITIALIZED != fm_state) {
		FM_ERR_REPORT("cg2900_fm_deinit: Already de-Initialized");
		result = -EINVAL;
		goto error;
	}
	fmd_exit();
	mutex_destroy(&rds_mutex);
	fm_state = CG2900_FM_STATE_DEINITIALIZED;
	fm_mode = CG2900_FM_IDLE_MODE;

error:
	FM_DEBUG_REPORT("cg2900_fm_deinit: returning %d",
			result);
	return result;
}

int cg2900_fm_switch_on(
			struct device *device
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_switch_on");

	if (CG2900_FM_STATE_INITIALIZED != fm_state) {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	/* Enable FM IP */
	FM_DEBUG_REPORT("cg2900_fm_switch_on: " "Sending FM IP Enable");

	if (fmd_send_fm_ip_enable()) {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
			      "Error in fmd_send_fm_ip_enable");
		result = -EINVAL;
		goto error;
	}

	if (version_info.revision == CG2910_PG1_REV
			|| version_info.revision == CG2910_PG1_05_REV
			|| version_info.revision == CG2910_PG2_REV
			|| version_info.revision == CG2905_PG1_05_REV
			|| version_info.revision == CG2905_PG2_REV) {
		/* Now Download CG2910 lut and program Firmware files */
		if (cg2910_fm_load_firmware(device) != 0) {
			FM_ERR_REPORT("cg2900_fm_switch_on: "
				"Error in downloading firmware for CG2910/05");
			result = -EINVAL;
			goto error;
		}
	} else if (version_info.revision == CG2900_PG1_REV
			|| version_info.revision == CG2900_PG2_REV
			|| version_info.revision == CG2900_PG1_SPECIAL_REV) {
		/* Now Download the Coefficient Files and FM Firmware */
		if (cg2900_fm_load_firmware(device) != 0) {
			FM_ERR_REPORT("cg2900_fm_switch_on: "
					"Error in downloading firmware for CG2900");
			result = -EINVAL;
			goto error;
		}
	} else {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
				"Unsupported Chip revision");
		result = -EINVAL;
		goto error;
	}

	/* Power up FM */
	result = fmd_power_up();
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
			      "fmd_power_up failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Switch Mode To Idle */
	result = fmd_set_mode(FMD_MODE_IDLE);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
			"fmd_set_mode failed %x",
			(unsigned int)result);
		result =  -EINVAL;
		goto error;
	}

	fm_state = CG2900_FM_STATE_SWITCHED_ON;
	fm_mode = CG2900_FM_IDLE_MODE;
	memset(&fm_rds_info, 0,
		sizeof(struct cg2900_fm_rds_info));
	memset(fm_rds_buf, 0,
		sizeof(struct cg2900_fm_rds_buf) *
		MAX_RDS_BUFFER * MAX_RDS_GROUPS);

error:
	FM_DEBUG_REPORT("cg2900_fm_switch_on: returning %d",
			result);
	return result;
}

int cg2900_fm_switch_off(void)
{
	int result = 0;

	FM_INFO_REPORT("cg2900_fm_switch_off");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state &&
		CG2900_FM_STATE_STAND_BY != fm_state) {
		FM_ERR_REPORT("cg2900_fm_switch_off: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	/* Stop the RDS Thread if it is running */
	if (fm_rds_status) {
		fm_rds_status = false;
		fmd_stop_rds_thread();
	}
	if (CG2900_FM_STATE_STAND_BY == fm_state) {
		/* Power up FM */
		result = fmd_power_up();
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_switch_off: "
			      "fmd_power_up failed %x",
			      (unsigned int)result);
			result = -EINVAL;
			goto error;
		} else
			fm_state = CG2900_FM_STATE_SWITCHED_ON;
	}
	if (fmd_send_fm_ip_disable()) {
		FM_ERR_REPORT("cg2900_fm_switch_off: "
			      "Problem in fmd_send_fm_ip_"
			      "disable");
		result = -EINVAL;
		goto error;
	}
	if (0 == result) {
		fm_state = CG2900_FM_STATE_INITIALIZED;
		fm_mode = CG2900_FM_IDLE_MODE;
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_switch_off: returning %d",
			result);
	return result;
}

int cg2900_fm_standby(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_standby");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_standby: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	memset(&fm_rds_info, 0,
		sizeof(struct cg2900_fm_rds_info));
	memset(fm_rds_buf, 0,
		sizeof(struct cg2900_fm_rds_buf) *
		MAX_RDS_BUFFER * MAX_RDS_GROUPS);
	/* Remove all Interrupts from the queue */
	skb_queue_purge(&fm_interrupt_queue);
	result = fmd_goto_standby();
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_standby: "
			      "FMLGotoStandby failed, "
			      "err = %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	fm_state = CG2900_FM_STATE_STAND_BY;

error:
	FM_DEBUG_REPORT("cg2900_fm_standby: returning %d",
			result);
	return result;
}

int cg2900_fm_power_up_from_standby(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_power_up_from_standby");

	if (CG2900_FM_STATE_STAND_BY != fm_state) {
		FM_ERR_REPORT("cg2900_fm_power_up_from_standby: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	/* Power up FM */
	result = fmd_power_up();
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_power_up_from_standby: "
			      "fmd_power_up failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	} else {
		fm_state = CG2900_FM_STATE_SWITCHED_ON;
		if (CG2900_FM_TX_MODE == fm_mode) {
			/* Enable the PA */
			result = fmd_tx_set_pa(true);
			if (0 != result) {
				FM_ERR_REPORT
				    ("cg2900_fm_power_up_from_standby:"
				     " fmd_tx_set_pa " "failed %d",
				     (unsigned int)result);
				result = -EINVAL;
				goto error;
			}
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_power_up_from_standby: returning %d",
			result);
	return result;
}

int cg2900_fm_set_rx_default_settings(
			u32 freq,
			u8 band,
			u8 grid,
			bool enable_rds,
			bool enable_stereo
			)
{
	int result;
	u8 vol_percent;

	FM_INFO_REPORT("cg2900_fm_set_rx_default_settings: freq = %d Hz, "
		       "band = %d, grid = %d, RDS = %d, Stereo Mode = %d",
		       freq, band, grid, enable_rds, enable_stereo);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state &&
		CG2900_FM_STATE_STAND_BY != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_STATE_STAND_BY == fm_state) {
		/* Power up FM */
		result = fmd_power_up();
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "fmd_power_up failed %x",
			      (unsigned int)result);
			result = -EINVAL;
			goto error;
		} else
			fm_state = CG2900_FM_STATE_SWITCHED_ON;
	}
	fm_mode = CG2900_FM_RX_MODE;

	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: "
			"Sending Set mode to Rx");
	result = fmd_set_mode(FMD_MODE_RX);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "fmd_set_mode failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Grid */
	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: "
			"Sending fmd_rx_set_grid ");
	result = fmd_rx_set_grid(grid);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "fmd_rx_set_grid failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Band */
	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: "
			"Sending Set fmd_set_freq_range");
	result = fmd_set_freq_range(band);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "fmd_set_freq_range failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Frequency */
	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: "
			"Sending Set fmd_rx_set_frequency");
	result = fmd_rx_set_frequency(
			freq / FREQUENCY_CONVERTOR_KHZ_HZ);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "fmd_rx_set_frequency failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: "
			"SetFrequency interrupt received, "
			"Sending Set fmd_rx_set_stereo_mode");

	if (enable_stereo) {
		/* Set the Stereo Blending mode */
		result = fmd_rx_set_stereo_mode(
			FMD_STEREOMODE_BLENDING);
	} else {
		/* Set the Mono mode */
		result = fmd_rx_set_stereo_mode(
			FMD_STEREOMODE_MONO);
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "fmd_rx_set_stereo_mode "
			      "failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	if (enable_stereo) {
		/* Set the Stereo Blending RSSI control */
		result = fmd_rx_set_stereo_ctrl_blending_rssi(
			STEREO_BLENDING_MIN_RSSI,
			STEREO_BLENDING_MAX_RSSI);
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			"fmd_rx_set_stereo_ctrl_blending_rssi "
			"failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set RDS Group rejection Off */
	result = fmd_rx_set_rds_group_rejection(
		FMD_RDS_GROUP_REJECTION_OFF);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			"fmd_rx_set_rds_group_rejection "
			"failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Remove all Interrupt from the queue */
	skb_queue_purge(&fm_interrupt_queue);

	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: "
			"Sending Set rds");

	if (enable_rds) {
		/* Enable RDS */
		a_b_flag = false;
		result = cg2900_fm_rds_on();
	} else {
		/* Disable RDS */
		result = cg2900_fm_rds_off();
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rx_default_settings: "
			      "cg2900_fm_rds_on "
			      "failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Currently, not supported for CG2905/10 */
	if (version_info.revision == CG2900_PG1_REV
			|| version_info.revision == CG2900_PG2_REV
			|| version_info.revision == CG2900_PG1_SPECIAL_REV) {
		/* Set the Analog Out Volume to Max */
		vol_percent = (u8)
			(((u16) (MAX_ANALOG_VOLUME) * 100)
			/ MAX_ANALOG_VOLUME);
		result = fmd_set_volume(vol_percent);
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_switch_on: "
					"FMRSetVolume failed %x",
					(unsigned int)result);
			result = -EINVAL;
			goto error;
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_rx_default_settings: returning %d",
			result);
	return result;
}

int cg2900_fm_set_tx_default_settings(
			u32 freq,
			u8 band,
			u8 grid,
			bool enable_rds,
			bool enable_stereo
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_set_tx_default_settings: freq = %d Hz, "
		       "band = %d, grid = %d, RDS = %d, Stereo Mode = %d",
		       freq, band, grid, enable_rds, enable_stereo);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state &&
		CG2900_FM_STATE_STAND_BY != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_STATE_STAND_BY == fm_state) {
		/* Power up FM */
		result = fmd_power_up();
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_power_up failed %x",
			      (unsigned int)result);
			result = -EINVAL;
			goto error;
		} else
			fm_state = CG2900_FM_STATE_SWITCHED_ON;
	}
	fm_mode = CG2900_FM_TX_MODE;
	if (fm_rds_status) {
		fm_rds_status = false;
		fmd_stop_rds_thread();
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Give 50 ms delay to exit the RDS thread */
		schedule_timeout_interruptible(msecs_to_jiffies(50));
	}
	/* Remove all Interrupt from the queue */
	skb_queue_purge(&fm_interrupt_queue);

	/* Switch To Tx mode */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending Set mode to Tx");
	result = fmd_set_mode(FMD_MODE_TX);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_set_mode failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Sets the Limiter Values */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending fmd_limiter_setcontrol");
	result = fmd_limiter_setcontrol(
				DEFAULT_AUDIO_DEVIATION,
				DEFAULT_NOTIFICATION_HOLD_OFF_TIME);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_limiter_setcontrol failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Grid */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending fmd_tx_set_grid ");
	result = fmd_tx_set_grid(grid);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_tx_set_grid failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Band */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending fmd_tx_set_freq_range");
	result = fmd_tx_set_freq_range(band);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_tx_set_freq_range failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Band */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending fmd_tx_set_preemphasis");
	result = fmd_tx_set_preemphasis(FMD_EMPHASIS_75US);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
			      "fmd_tx_set_preemphasis failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Set the Frequency */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending Set fmd_tx_set_frequency");
	result = fmd_tx_set_frequency(
				freq / FREQUENCY_CONVERTOR_KHZ_HZ);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_switch_on: "
			      "fmd_tx_set_frequency failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"SetFrequency interrupt received, "
			"Sending Set fmd_tx_enable_stereo_mode");

	/* Set the Stereo mode */
	result = fmd_tx_enable_stereo_mode(enable_stereo);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_tx_enable_stereo_mode "
			      "failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending Set fmd_tx_set_pa");

	/* Enable the PA */
	result = fmd_tx_set_pa(true);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_tx_set_pa "
			      "failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"set PA interrupt received, "
			"Sending Set fmd_tx_set_signal_strength");

	/* Set the Signal Strength to Max */
	result = fmd_tx_set_signal_strength(
				MAX_POWER_LEVEL);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "fmd_tx_set_signal_strength "
			      "failed %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	/* Enable Tx RDS  */
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: "
			"Sending Set cg2900_fm_tx_rds");
	result = cg2900_fm_tx_rds(enable_rds);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_tx_default_settings: "
			      "cg2900_fm_tx_rds "
			      "failed %x", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_tx_default_settings: returning %d",
			result);
	return result;
}

int cg2900_fm_set_grid(
			u8 grid
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_set_grid: Grid = %d", grid);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_grid: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_rx_set_grid(grid);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_grid: "
			      "fmd_rx_set_grid failed");
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_grid: returning %d",
			result);
	return result;
}

int cg2900_fm_set_band(
			u8 band
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_set_band: Band = %d", band);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_band: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_set_freq_range(band);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_band: "
			      "fmd_set_freq_range failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_band: returning %d",
			result);
	return result;
}

int cg2900_fm_search_up_freq(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_search_up_freq");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_search_up_freq: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (fm_rds_status) {
		/* Stop RDS if it is active */
		result = cg2900_fm_rds_off();
		fm_prev_rds_status = true;
	} else {
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
	}
	result = fmd_rx_seek(CG2900_DIR_UP);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_search_up_freq: "
			      "Error Code %d", (unsigned int)result);
		cg2900_fm_check_rds_status();
		result = -EINVAL;
		goto error;
	}
	result = 0;

error:
	FM_DEBUG_REPORT("cg2900_fm_search_up_freq: returning %d",
			result);
	return result;
}

int cg2900_fm_search_down_freq(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_search_down_freq");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_search_down_freq: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (fm_rds_status) {
		/* Stop RDS if it is active */
		result = cg2900_fm_rds_off();
		fm_prev_rds_status = true;
	} else {
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
	}
	result = fmd_rx_seek(CG2900_DIR_DOWN);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_search_down_freq: "
			      "Error Code %d", (unsigned int)result);
		cg2900_fm_check_rds_status();
		result = -EINVAL;
		goto error;
	}
	result = 0;

error:
	FM_DEBUG_REPORT("cg2900_fm_search_down_freq: returning %d",
			result);
	return result;
}

int cg2900_fm_start_band_scan(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_start_band_scan");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_start_band_scan: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (fm_rds_status) {
		/* Stop RDS if it is active */
		result = cg2900_fm_rds_off();
		fm_prev_rds_status = true;
	} else {
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
	}
	result = fmd_rx_scan_band(DEFAULT_CHANNELS_TO_SCAN);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_start_band_scan: "
			      "Error Code %d", (unsigned int)result);
		cg2900_fm_check_rds_status();
		result = -EINVAL;
		goto error;
	}
	result = 0;

error:
	FM_DEBUG_REPORT("cg2900_fm_start_band_scan: returning %d",
			result);
	return result;
}

int cg2900_fm_stop_scan(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_stop_scan");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_stop_scan: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_rx_stop_seeking();
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_stop_scan: "
			      "Error Code %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	memset(&fm_rds_info, 0,
	       sizeof(struct cg2900_fm_rds_info));
	memset(fm_rds_buf, 0,
	       sizeof(struct cg2900_fm_rds_buf) *
	       MAX_RDS_BUFFER * MAX_RDS_GROUPS);
	result = 0;
	if (fm_prev_rds_status) {
		/* Restart RDS if it was active earlier */
		cg2900_fm_rds_on();
		fm_prev_rds_status = false;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_stop_scan: returning %d",
			result);
	return result;
}

int cg2900_fm_get_scan_result(
			u16 *num_of_scanfreq,
			u32 *scan_freq,
			u32 *scan_freq_rssi_level
			)
{
	int result;
	u32 cnt;
	u32 index;
	u32 minfreq;
	u32 maxfreq;
	u16 channels[3];
	u16 rssi[3];
	u8 freq_range;
	u8 max_channels = 0;

	FM_INFO_REPORT("cg2900_fm_get_scan_result");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_scan_result: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_get_freq_range(&freq_range);

	if (0 != result) {
		result = -EINVAL;
		goto error;
	}

	result = fmd_get_freq_range_properties(
				freq_range,
				&minfreq,
				&maxfreq);

	if (0 != result) {
		result = -EINVAL;
		goto error;
	}

	result = fmd_rx_get_max_channels_to_scan(&max_channels);

	if (0 != result) {
		result = -EINVAL;
		goto error;
	}

	/* In 1 iteration we can retreive max 3 channels */
	cnt = (max_channels / 3) + 1;
	while ((cnt--) && (result == 0)) {
		/*
		 * Get all channels, including empty ones.
		 * In 1 iteration at max 3 channels can be found.
		 */
		result = fmd_rx_get_scan_band_info(cnt * 3,
						   num_of_scanfreq,
						   channels, rssi);
		if (0 == result) {
			index = cnt * 3;
			/* Convert Freq to Hz from channel number */
			scan_freq[index] = (minfreq +
					channels[0] *
					CHANNEL_FREQ_CONVERTER_MHZ) *
					FREQUENCY_CONVERTOR_KHZ_HZ;
			scan_freq_rssi_level[index] = rssi[0];
			/* Convert Freq to Hz from channel number */
			scan_freq[index + 1] = (minfreq +
						channels[1] *
						CHANNEL_FREQ_CONVERTER_MHZ) *
						FREQUENCY_CONVERTOR_KHZ_HZ;
			scan_freq_rssi_level[index + 1] = rssi[1];
			/* Check if we donot overwrite the array */
			if (cnt < (max_channels / 3)) {
				/* Convert Freq to Hz from channel number */
				scan_freq[index + 2] = (minfreq +
					channels[2] *
					CHANNEL_FREQ_CONVERTER_MHZ) *
					FREQUENCY_CONVERTOR_KHZ_HZ;
				scan_freq_rssi_level[index + 2]
				    = rssi[2];
			}
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_scan_result: returning %d",
			result);
	return result;

}

int cg2900_fm_start_block_scan(
			u32 start_freq,
			u32 end_freq
			)
{
	int result;
	u8 antenna;

	FM_INFO_REPORT("cg2900_fm_start_block_scan");

	FM_DEBUG_REPORT("cg2900_fm_start_block_scan: Start Freq = %d, "
		"End Freq = %d", start_freq, end_freq);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_start_block_scan: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (fm_rds_status) {
		/* Stop RDS if it is active */
		result = cg2900_fm_rds_off();
		fm_prev_rds_status = true;
	} else {
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
	}
	result = fmd_get_antenna(
			&antenna);
	result = fmd_block_scan(
			start_freq/FREQUENCY_CONVERTOR_KHZ_HZ,
			end_freq/FREQUENCY_CONVERTOR_KHZ_HZ,
			antenna);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_start_block_scan: "
			"Error Code %d", (unsigned int)result);
		cg2900_fm_check_rds_status();
		result = -EINVAL;
		goto error;
	}
	result = 0;

error:
	FM_DEBUG_REPORT("cg2900_fm_start_block_scan: returning %d",
			result);
	return result;
}

int cg2900_fm_get_block_scan_result(
			u16 *num_of_scanchan,
			u16 *scan_freq_rssi_level
			)
{
	int result = 0;
	u32 cnt;
	u32 index;
	u16 rssi[6];

	FM_INFO_REPORT("cg2900_fm_get_block_scan_result");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_block_scan_result: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	cnt = 33;
	while ((cnt--) && (result == 0)) {
		/* Get all channels, including empty ones */
		result = fmd_get_block_scan_result(
						      cnt * 6,
						      num_of_scanchan,
						      rssi);
		if (0 == result) {
			index = cnt * 6;
			scan_freq_rssi_level[index]
			    = rssi[0];
			scan_freq_rssi_level[index + 1]
			    = rssi[1];
			scan_freq_rssi_level[index + 2]
			    = rssi[2];
			scan_freq_rssi_level[index + 3]
			    = rssi[3];
			scan_freq_rssi_level[index + 4]
			    = rssi[4];
			scan_freq_rssi_level[index + 5]
			    = rssi[5];
		}
	}
	if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_block_scan_result:"
				" Sending Set fmd_tx_set_pa");

		/* Enable the PA */
		result = fmd_tx_set_pa(true);
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_get_block_scan_result:"
				      " fmd_tx_set_pa "
				      "failed %d",
				      (unsigned int)result);
			result = -EINVAL;
			goto error;
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_block_scan_result: returning %d",
			result);
	return result;

}

int cg2900_fm_tx_rds(
			bool enable_rds
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_rds: enable_rds = %d", enable_rds);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_rds: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (enable_rds) {
		/* Set the Tx Buffer Size */
		result = fmd_tx_buffer_set_size(
						MAX_RDS_GROUPS - 2);
	if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_tx_rds: "
				      "fmd_tx_buffer_set_size "
				      "failed %d",
				      (unsigned int)result);
			result = -EINVAL;
			goto error;
		} else {
			result = fmd_tx_set_rds(true);
		}

	if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_tx_rds: "
				      "fmd_tx_set_rds "
				      "failed %d",
				      (unsigned int)result);
			result = -EINVAL;
			goto error;
		}
		program_identification_code =
		    default_program_identification_code;
		program_type_code = default_program_type_code;
		memcpy(program_service,
			    default_program_service,
			    MAX_PSN_SIZE);
		memcpy(radio_text,
			    default_radio_text, MAX_RT_SIZE);
		radio_text[strlen(radio_text)] = 0x0D;
		cg2900_fm_transmit_rds_groups();
		result = 0;
	} else {
		result = fmd_tx_set_rds(false);

	if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_tx_rds: "
				      "fmd_tx_set_rds "
				      "failed %d",
				      (unsigned int)result);
			result = -EINVAL;
			goto error;
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_rds: returning %d",
			result);

	return result;
}

int cg2900_fm_tx_set_pi_code(
			u16 pi_code
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_pi_code: PI = %04x", pi_code);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_pi_code: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	program_identification_code = pi_code;
	result = cg2900_fm_transmit_rds_groups();

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_pi_code: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_pty_code(
			u16 pty_code
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_pty_code: PTY = %04x", pty_code);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_pty_code: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	program_type_code = pty_code;
	result = cg2900_fm_transmit_rds_groups();

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_pty_code: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_program_station_name(
			char *psn,
			u8 len
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_program_station_name: PSN = %s",
			psn);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_program_station_name: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (len < (MAX_PSN_SIZE - 1)) {
		int count = len;
		while (count < (MAX_PSN_SIZE - 1))
			psn[count++] = ' ';
	}
	memcpy(program_service, psn, MAX_PSN_SIZE);
	result = cg2900_fm_transmit_rds_groups();

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_program_station_name: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_radio_text(
			char *rt,
			u8 len
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_radio_text: RT = %s", rt);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_radio_text: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	rt[len] = 0x0D;
	memcpy(radio_text, rt, len + 1);

	result = cg2900_fm_transmit_rds_groups();

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_radio_text: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_get_rds_deviation(
			u16 *deviation
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_get_rds_deviation");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_get_rds_deviation: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_get_rds_deviation(deviation);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_get_rds_deviation: "
			      "fmd_tx_get_rds_deviation failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_get_rds_deviation: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_rds_deviation(
			u16 deviation
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_rds_deviation: deviation = %d",
		       deviation);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_rds_deviation: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_set_rds_deviation(deviation);
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_tx_set_rds_deviation: "
			      "fmd_tx_set_rds_deviation failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_rds_deviation: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_get_pilot_tone_status(
			bool *enable
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_get_pilot_tone_status");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_get_pilot_tone_status: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_get_stereo_mode(enable);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_get_pilot_tone_status: "
			"fmd_tx_get_stereo_mode failed %d",
			result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_get_pilot_tone_status: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_pilot_tone_status(
			bool enable
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_pilot_tone_status: enable = %d",
			enable);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_pilot_tone_status: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_enable_stereo_mode(enable);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_set_pilot_tone_status: "
			      "fmd_tx_enable_stereo_mode failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_pilot_tone_status: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_get_pilot_deviation(
			u16 *deviation
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_get_pilot_deviation");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_get_pilot_deviation: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_get_pilot_deviation(deviation);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_get_pilot_deviation: "
			      "fmd_tx_get_pilot_deviation failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_get_pilot_deviation: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_pilot_deviation(
			u16 deviation
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_pilot_deviation: deviation = %d",
		       deviation);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_pilot_deviation: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_set_pilot_deviation(deviation);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_set_pilot_deviation: "
			      "fmd_tx_set_pilot_deviation failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_pilot_deviation: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_get_preemphasis(
			u8 *preemphasis
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_get_preemphasis");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_get_preemphasis: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_get_preemphasis(preemphasis);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_get_preemphasis: "
			      "fmd_tx_get_preemphasis failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_get_preemphasis: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_preemphasis(
			u8 preemphasis
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_preemphasis: preemphasis = %d",
		       preemphasis);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_preemphasis: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_set_preemphasis(preemphasis);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_set_preemphasis: "
			      "fmd_tx_set_preemphasis failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_preemphasis: returning %d",
			result);
	return result;
}

int cg2900_fm_rx_set_deemphasis(
			u8 deemphasis
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_rx_set_deemphasis: deemphasis = %02x",
		deemphasis);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_rx_set_deemphasis: "
			"Invalid state of FM Driver = %d", fm_state);
			result = -EINVAL;
		goto error;
	}
	result = fmd_rx_set_deemphasis(deemphasis);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_rx_set_deemphasis: "
			"fmd_rx_set_deemphasis failed %d",
			result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_rx_set_deemphasis: returning %d", result);
	return result;
}

int cg2900_fm_tx_get_power_level(
			u16 *power_level
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_get_power_level");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_get_power_level: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_get_signal_strength(power_level);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_get_power_level: "
			      "fmd_tx_get_signal_strength failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_get_power_level: returning %d",
			result);
	return result;
}

int cg2900_fm_tx_set_power_level(
			u16 power_level
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_tx_set_power_level: power_level = %d",
		       power_level);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_tx_set_power_level: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_tx_set_signal_strength(power_level);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_tx_set_power_level: "
			      "fmd_tx_set_preemphasis failed %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_tx_set_power_level: returning %d",
			result);
	return result;
}

int cg2900_fm_set_audio_balance(
			s8 balance
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_set_audio_balance, balance = %d", balance);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_audio_balance: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_set_balance(balance);
	if (0 != result) {
		FM_ERR_REPORT("FMRSetAudioBalance : "
			      "Failed in fmd_set_balance, err = %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_audio_balance: returning %d",
			result);
	return result;
}

int cg2900_fm_set_aup_bt_setvolume(
		u8 vol_level
		)
{
    int result;
    u8 vol_percent;

    FM_INFO_REPORT("cg2900_fm_set_aup_bt_setvolume: Volume Level = %d", vol_level);

    if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
        FM_ERR_REPORT("cg2900_fm_set_aup_bt_setvolume: "
                "Invalid state of FM Driver = %d", fm_state);
        result = -EINVAL;
        goto error;
    }
    vol_percent =
            (u8) (((u16) (vol_level) * 100) / MAX_ANALOG_VOLUME);
    result = fmd_bt_set_volume(vol_percent);
    if (0 != result) {
        FM_ERR_REPORT("cg2900_fm_set_aup_bt_setvolume: "
                "FMRSetVolume failed, err = %d",
                (unsigned int)result);
        result = -EINVAL;
        goto error;
    }

    error:
    FM_DEBUG_REPORT("cg2900_fm_set_aup_bt_setvolume: returning %d",
			result);
	return result;
}

int cg2900_fm_set_volume(
			u8 vol_level
			)
{
	int result;
	u8 vol_percent;

	FM_INFO_REPORT("cg2900_fm_set_volume: Volume Level = %d", 
			vol_level);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_volume: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	vol_percent =
	    (u8) (((u16) (vol_level) * 100) / MAX_ANALOG_VOLUME);
	result = fmd_set_volume(vol_percent);
	if (result) {
		FM_ERR_REPORT("cg2900_fm_increase_volume: "
			      "FMRSetVolume failed, err = %d",
			      result);
		result = -EINVAL;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_volume: returning %d",
			result);
	return result;
}

int cg2900_fm_get_volume(
			u8 *vol_level
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_get_volume");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_volume: "
			"Invalid state of FM Driver = %d", fm_state);
		*vol_level = 0;
		result = -EINVAL;
		goto error;
	}
	result = fmd_get_volume(vol_level);

error:
	FM_DEBUG_REPORT("cg2900_fm_get_volume: returning %d, VolLevel = %d",
			result, *vol_level);
	return result;
}

int cg2900_fm_rds_off(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_rds_off");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_rds_off: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	memset(&fm_rds_info, 0,
	       sizeof(struct cg2900_fm_rds_info));
	memset(fm_rds_buf, 0,
	       sizeof(struct cg2900_fm_rds_buf) *
	       MAX_RDS_BUFFER * MAX_RDS_GROUPS);
	result = fmd_rx_set_rds(FMD_SWITCH_OFF_RDS);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_rds_off: fmd_rx_set_rds failed, "
			"err = %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	if (fm_rds_status) {
		/* Stop the RDS Thread */
		FM_DEBUG_REPORT("cg2900_fm_rds_off: "
				"Stopping RDS Thread");
		fmd_stop_rds_thread();
		fm_rds_status = false;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_rds_off: returning %d",
			result);
	return result;
}

int cg2900_fm_rds_on(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_rds_on");
	if (fm_rds_status) {
		result = 0;
		FM_DEBUG_REPORT("cg2900_fm_rds_on: rds is on "
			"return result = %d", result);
		return result;
	}
	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_rds_on: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	FM_DEBUG_REPORT("cg2900_fm_rds_on:"
			" Sending fmd_rx_buffer_set_size");
	result = fmd_rx_buffer_set_size(MAX_RDS_GROUPS);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_rds_on: fmd_rx_buffer_set_size"
			"failed, err = %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	FM_DEBUG_REPORT("cg2900_fm_rds_on: Sending "
			"fmd_rx_buffer_set_threshold");
	result = fmd_rx_buffer_set_threshold(MAX_RDS_GROUPS - 1);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_rds_on: fmd_rx_buffer_set_threshold "
			"failed, err = %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	FM_DEBUG_REPORT("cg2900_fm_rds_on: Sending fmd_rx_set_rds");
	result = fmd_rx_set_rds(FMD_SWITCH_ON_RDS_ENHANCED_MODE);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_rds_on: fmd_rx_set_rds failed, "
			"err = %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	/* Start the RDS Thread to read the RDS Buffers */
	fm_rds_status = true;
	memset(&fm_rds_info, 0,
		sizeof(struct cg2900_fm_rds_info));
	memset(fm_rds_buf, 0,
		sizeof(struct cg2900_fm_rds_buf) *
		MAX_RDS_BUFFER * MAX_RDS_GROUPS);
	fmd_start_rds_thread(cg2900_fm_rds_callback);

error:
	FM_DEBUG_REPORT("cg2900_fm_rds_on: returning %d",
			result);
	return result;
}

int cg2900_fm_get_rds_status(
			bool *rds_status
			)
{
	int result = 0;

	FM_INFO_REPORT("cg2900_fm_get_rds_status");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_rds_status: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_RX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_rds_status: "
				"fmd_rx_get_rds");
		result = fmd_rx_get_rds(rds_status);
	} else if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_rds_status: "
				"fmd_tx_get_rds");
		result = fmd_tx_get_rds(rds_status);
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_get_rds_status: "
			      "fmd_get_rds failed, Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_rds_status: returning %d, "
			"rds_status = %d", result,
			*rds_status);
	return result;
}

int cg2900_fm_mute(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_mute");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_mute: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}

	if (version_info.revision == CG2900_PG1_REV
	        || version_info.revision == CG2900_PG2_REV
	        || version_info.revision == CG2900_PG1_SPECIAL_REV) {
		/* Mute Analog DAC */
		result = fmd_set_mute(true);
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_mute: "
					  "fmd_set_mute failed %d",
					  (unsigned int)result);
			result = -EINVAL;
			goto error;
		}
	} else {
		/* Mute BT Sample Rate Converter */
		result = fmd_bt_set_mute(true);
		if (0 != result) {
			result = -EINVAL;
			goto error;
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_mute: returning %d",
			result);
	return result;
}

int cg2900_fm_unmute(void)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_unmute");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_unmute: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}

	if (version_info.revision == CG2900_PG1_REV
	        || version_info.revision == CG2900_PG2_REV
	        || version_info.revision == CG2900_PG1_SPECIAL_REV) {

		/* Unmute Analog DAC */
		result = fmd_set_mute(false);
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_unmute: "
		      "cg2900_fm_unmute failed %d",
		      (unsigned int)result);
		result = -EINVAL;
		goto error;
		}
	} else {
		/* UnMute BT Sample Rate Converter */
		result = fmd_bt_set_mute(false);
		if (0 != result) {
			result = -EINVAL;
			goto error;
		}
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_unmute: returning %d",
			result);
	return result;
}

int cg2900_fm_get_frequency(
			u32 *freq
			)
{
	int result = 0;
	u32 currentFreq = 0;

	FM_INFO_REPORT("cg2900_fm_get_frequency");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_frequency: "
			"Invalid state of FM Driver = %d", fm_state);
		*freq = 0;
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_RX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_frequency: "
				"fmd_rx_get_frequency");
		result = fmd_rx_get_frequency(
					      (u32 *) &currentFreq);
	} else if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_frequency: "
				"fmd_tx_get_frequency");
		result = fmd_tx_get_frequency(
					      (u32 *) &currentFreq);
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_get_frequency: "
			      "fmd_rx_get_frequency failed %d",
			      (unsigned int)result);
		*freq = 0;
		result = -EINVAL;
		goto error;
	}
	/* Convert To Hz */
	*freq = currentFreq * FREQUENCY_CONVERTOR_KHZ_HZ;
	FM_DEBUG_REPORT("cg2900_fm_get_frequency: "
			"Current Frequency = %d Hz", *freq);

error:
	FM_DEBUG_REPORT("cg2900_fm_get_frequency: returning %d",
			result);
	return result;
}

int cg2900_fm_set_frequency(
			u32 new_freq
			)
{
	int result = 0;

	FM_INFO_REPORT("cg2900_fm_set_frequency, new_freq = %d",
			(int)new_freq);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_frequency: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	/* Check if RDS needs to be disabled before Setting Frequency */
	if (fm_rds_status) {
		/* Stop RDS if it is active */
		result = cg2900_fm_rds_off();
		fm_prev_rds_status = true;
	} else {
		memset(&fm_rds_info, 0,
			sizeof(struct cg2900_fm_rds_info));
		memset(fm_rds_buf, 0,
			sizeof(struct cg2900_fm_rds_buf) *
			MAX_RDS_BUFFER * MAX_RDS_GROUPS);
		/* Remove all Interrupts from the queue */
		skb_queue_purge(&fm_interrupt_queue);
	}

	if (CG2900_FM_RX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_set_frequency: "
				"fmd_rx_set_frequency");
		result = fmd_rx_set_frequency(
				new_freq / FREQUENCY_CONVERTOR_KHZ_HZ);
	} else if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_set_frequency: "
				"fmd_tx_set_frequency");
		result = fmd_tx_set_frequency(
				new_freq / FREQUENCY_CONVERTOR_KHZ_HZ);
	}
	if (fm_prev_rds_status) {
		/* Restart RDS if it was active earlier */
		cg2900_fm_rds_on();
		fm_prev_rds_status = false;
	}
	if (result != 0) {
		FM_ERR_REPORT("cg2900_fm_set_frequency: "
			      "fmd_rx_set_frequency failed %x",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

	if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_set_frequency:"
				" Sending Set" "fmd_tx_set_pa");

		/* Enable the PA */
		result = fmd_tx_set_pa(true);
		if (0 != result) {
			FM_ERR_REPORT("cg2900_fm_set_frequency:"
				      " fmd_tx_set_pa "
				      "failed %d",
				      (unsigned int)result);
			result = -EINVAL;
			goto error;
		}
		result = 0;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_frequency: returning %d",
			result);
	return result;
}

int cg2900_fm_get_signal_strength(
			u16 *signal_strength
			)
{
	int result = 0;

	FM_INFO_REPORT("cg2900_fm_get_signal_strength");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_signal_strength: "
			"Invalid state of FM Driver = %d", fm_state);
		*signal_strength = 0;
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_RX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_signal_strength: "
				"fmd_rx_get_signal_strength");
		result = fmd_rx_get_signal_strength(
						    signal_strength);
	} else if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_signal_strength: "
				"fmd_tx_get_signal_strength");
		result = fmd_tx_get_signal_strength(
						    signal_strength);
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_get_signal_strength: "
			      "Error Code %d", (unsigned int)result);
		*signal_strength = 0;
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_signal_strength: returning %d",
			result);
	return result;
}

int cg2900_fm_af_update_get_result(
			u16 *af_update_rssi
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_af_update_get_result");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_af_update_get_result: "
			"Invalid state of FM Driver = %d", fm_state);
		*af_update_rssi = 0;
		result = -EINVAL;
		goto error;
	}
	result = fmd_rx_get_af_update_result(af_update_rssi);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_af_update_get_result: "
			      "Error Code %d", (unsigned int)result);
		*af_update_rssi = 0;
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_af_update_get_result: returning %d",
			result);
	return result;
}

int cg2900_fm_af_update_start(
			u32 af_freq
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_af_update_start");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_af_update_start: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_rx_af_update_start(
					af_freq / FREQUENCY_CONVERTOR_KHZ_HZ);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_af_update_start: "
			      "Error Code %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_af_update_start: returning %d",
			result);
	return result;
}

int cg2900_fm_af_switch_get_result(
			u16 *af_switch_conclusion
			)
{
	int result;
	u16 af_rssi;
	u16 af_pi;

	FM_INFO_REPORT("cg2900_fm_af_switch_get_result");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_af_switch_get_result: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_rx_get_af_switch_results(
					      af_switch_conclusion,
					      &af_rssi, &af_pi);

	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_af_switch_get_result: "
			      "Error Code %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}
	FM_DEBUG_REPORT("cg2900_fm_af_switch_get_result: "
			"AF Switch conclusion = %d "
			"AF Switch RSSI level = %d "
			"AF Switch PI code = %d ",
			*af_switch_conclusion, af_rssi, af_pi);

error:
	FM_DEBUG_REPORT("cg2900_fm_af_switch_get_result: returning %d",
			result);
	return result;

}

int cg2900_fm_af_switch_start(
			u32 af_switch_freq,
			u16 af_switch_pi
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_af_switch_start");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_af_switch_start: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_rx_af_switch_start(
				af_switch_freq / FREQUENCY_CONVERTOR_KHZ_HZ,
				af_switch_pi);

	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_af_switch_start: "
			      "Error Code %d", (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_af_switch_start: returning %d",
			result);
	return result;
}

int cg2900_fm_get_mode(
			u8 *cur_mode
			)
{
	int result = 0;
	bool stereo_mode;

	FM_INFO_REPORT("cg2900_fm_get_mode");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_mode: "
			"Invalid state of FM Driver = %d", fm_state);
		*cur_mode = CG2900_MODE_MONO;
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_RX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_mode: "
				"fmd_rx_get_stereo_mode");
		result = fmd_rx_get_stereo_mode(cur_mode);
		FM_DEBUG_REPORT("cg2900_fm_get_mode: cur_mode = %x", *cur_mode);
	} else if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_get_mode: "
				"fmd_tx_get_stereo_mode");
		result = fmd_tx_get_stereo_mode(&stereo_mode);
		if (stereo_mode)
			*cur_mode = CG2900_MODE_STEREO;
		else
			*cur_mode = CG2900_MODE_MONO;
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_get_mode: "
			      "fmd_get_stereo_mode failed, "
			      "Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_mode: returning %d, mode = %d",
			result, *cur_mode);
	return result;
}

int cg2900_fm_set_mode(
			u8 mode
			)
{
	int result = 0;
	bool enable_stereo_mode = false;

	FM_INFO_REPORT("cg2900_fm_set_mode: mode = %d", mode);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_set_mode: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	if (CG2900_FM_RX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_set_mode: "
				"fmd_rx_set_stereo_mode");
		result = fmd_rx_set_stereo_mode(mode);
	} else if (CG2900_FM_TX_MODE == fm_mode) {
		FM_DEBUG_REPORT("cg2900_fm_set_mode: "
				"fmd_tx_set_stereo_mode");
		if (mode == CG2900_MODE_STEREO)
			enable_stereo_mode = true;
		result =
		    fmd_tx_enable_stereo_mode(
					      enable_stereo_mode);
	}
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_mode: "
			      "fmd_rx_set_stereo_mode failed, "
			      "Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_mode: returning %d",
		result);
	return result;
}

int cg2900_fm_select_antenna(
			u8 antenna
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_select_antenna: Antenna = %d", antenna);

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_select_antenna: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_set_antenna(antenna);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_select_antenna: "
			      "fmd_set_antenna failed, Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_select_antenna: returning %d",
			result);
	return result;
}

int cg2900_fm_get_antenna(
			u8 *antenna
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_get_antenna");

	if (CG2900_FM_STATE_SWITCHED_ON != fm_state) {
		FM_ERR_REPORT("cg2900_fm_get_antenna: "
			"Invalid state of FM Driver = %d", fm_state);
		result = -EINVAL;
		goto error;
	}
	result = fmd_get_antenna(antenna);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_get_antenna: "
			      "fmd_get_antenna failed, Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_antenna: returning %d",
			result);
	return result;
}

int cg2900_fm_get_rssi_threshold(
			u16 *rssi_thresold
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_get_rssi_threshold");

	result = fmd_rx_get_stop_level(rssi_thresold);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_get_rssi_threshold: "
			      "fmd_rx_get_stop_level failed, Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_get_rssi_threshold: returning %d",
			result);
	return result;
}

int cg2900_fm_set_rssi_threshold(
			u16 rssi_thresold
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_set_rssi_threshold: "
		       "RssiThresold = %d", rssi_thresold);

	result = fmd_rx_set_stop_level(rssi_thresold);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_rssi_threshold: "
			      "fmd_rx_set_stop_level failed, Error Code %d",
			      (unsigned int)result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_rssi_threshold: returning %d",
			result);
	return result;
}

void cg2900_fm_set_chip_version(
			u16 revision,
			u16 sub_version
			)
{
	version_info.revision = revision;
	version_info.sub_version = sub_version;
}

int cg2900_fm_set_test_tone_generator(
			u8 test_tone_status
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_set_test_tone_generator: "
		"test_tone_status = %02x", test_tone_status);

	result = fmd_set_test_tone_generator_status(test_tone_status);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_set_test_tone_generator: "
			"fmd_set_test_tone_generator_status failed"
			", Error Code %d", result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_set_test_tone_generator: returning %d",
		result);
	return result;
}

int cg2900_fm_test_tone_connect(
			u8 left_audio_mode,
			u8 right_audio_mode
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_test_tone_connect: "
		"left_audio_mode = %02x right_audio_mode = %02x",
		left_audio_mode, right_audio_mode);

	result = fmd_test_tone_connect(left_audio_mode, right_audio_mode);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_test_tone_connect: "
			"fmd_set_test_tone_connect failed, Error Code %d",
			result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_test_tone_connect: returning %d",
		result);
	return result;
}

int cg2900_fm_test_tone_set_params(
			u8 tone_gen,
			u16 frequency,
			u16 volume,
			u16 phase_offset,
			u16 dc,
			u8 waveform
			)
{
	int result;

	FM_INFO_REPORT("cg2900_fm_test_tone_set_params: "
		"tone_gen = %02x frequency = %04x "
		"volume = %04x phase_offset = %04x "
		"dc offset = %04x waveform = %02x",
		tone_gen, frequency,
		volume, phase_offset,
		dc, waveform);

	result = fmd_test_tone_set_params(
				tone_gen,
				frequency,
				volume,
				phase_offset,
				dc,
				waveform);
	if (0 != result) {
		FM_ERR_REPORT("cg2900_fm_test_tone_set_params: "
			"fmd_test_tone_set_params failed, Error Code %d",
			result);
		result = -EINVAL;
		goto error;
	}

error:
	FM_DEBUG_REPORT("cg2900_fm_test_tone_set_params: returning %d",
		result);
	return result;
}

MODULE_AUTHOR("Hemant Gupta");
MODULE_LICENSE("GPL v2");
