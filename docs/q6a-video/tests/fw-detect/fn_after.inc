static bool iris_detect_gen2_from_fwdata(const u8 *data, size_t size)
{
	const char *marker = "QC_IMAGE_VERSION_STRING=";
	const size_t mlen = strlen(marker);
	int major = 0, minor = 0;
	char version_buf[64];
	size_t max;

	max = (size > mlen) ? size - mlen : 0;
	for (size_t i = 0; i < max; i++) {
		if (!memcmp(data + i, marker, mlen)) {
			size_t avail = min(sizeof(version_buf) - 1, size - (i + mlen));

			/* The string may sit at the very end of the blob: never read past it */
			memcpy(version_buf, data + i + mlen, avail);
			version_buf[avail] = '\0';
			if (!strncmp(version_buf, "vfw", 3))
				return true;
			if (sscanf(version_buf, "video-firmware.%d.%d", &major, &minor) == 2 &&
			    major >= 2)
				return true;
			break;
		}
	}

	return false;
}
