/*
 * eyefi-unix.c
 *
 * Copyright (C) 2008 Dave Hansen <dave@sr71.net>
 *
 * This software may be redistributed and/or modified under the terms of
 * the GNU General Public License ("GPL") version 2 as published by the
 * Free Software Foundation.
 */

#include "eyefi-config.h"

void print_pascal_string(struct pascal_string *str)
{
	int i;
	for (i = 0; i < str->length; i++)
		printf("%c", str->value[i]);
}

void print_mac(struct mac_address *mac)
{
	int i;
	for (i=0; i < MAC_BYTES-1; i++) {
		printf("%02x:", mac->mac[i]);
	}
	printf("%02x\n", mac->mac[i]);
}


void print_card_mac(void)
{
	debug_printf(2, "%s()\n", __func__);
	struct mac_address *mac;

	card_info_cmd(MAC_ADDRESS);
	mac = eyefi_response();
	assert(mac->length == MAC_BYTES);
	printf("card mac address: ");
	print_mac(mac);
}

void print_card_firmware_info(void)
{
	struct card_firmware_info *info = fetch_card_firmware_info();
	printf("card firmware (len: %d): '", info->info.length);
	print_pascal_string(&info->info);
	printf("'\n");
}

void print_card_key(void)
{
	debug_printf(2, "%s()\n", __func__);
	struct card_info_rsp_key *foo = fetch_card_key();
	printf("card key (len: %d): '", foo->key.length);
	print_pascal_string(&foo->key);
	printf("'\n");
}

void scan_print_nets(void)
{
	int i;

	debug_printf(2, "%s()\n", __func__);
	struct scanned_net_list *scanned = scan_nets();
	if (scanned->nr == 0) {
		printf("unable to detect any wireless networks\n");
		return;
	}
	printf("Scanned wireless networks:\n");
	for (i=0; i < scanned->nr; i++) {
		struct scanned_net *net = &scanned->nets[i];
		printf("'%s' type(%d): %s, strength: %d\n", net->essid,
				net->type,
				net_type_name(net->type),
				net->strength);
	}
}

void print_configured_nets(void)
{
	int ret;
	int i;
	struct configured_net_list *configured = fetch_configured_nets();

	debug_printf(2, "%s()\n", __func__);
	ret = issue_noarg_command('l');
	if (ret) {
		printf("error issuing print networks command: %d\n", ret);
		return;
	}
       	configured = eyefi_response();
	if (configured->nr == 0) {
		printf("No wireless networks configured on card\n");
		return;
	}
	printf("configured wireless networks:\n");
	for (i=0; i < configured->nr; i++) {
		struct configured_net *net = &configured->nets[i];
		printf("'%s'\n", net->essid);
	}
}

int try_connection_to(char *essid, char *wpa_ascii)
{
	int i;
	int ret = -1;
	const char *type;

       	type = net_type_name(NET_WPA);
	if (!wpa_ascii)
		type = net_type_name(NET_UNSECURED);

	eyefi_printf("trying to connect to network: '%s'", essid);
	eyefi_printf("of type: '%s'\n", type);
	if (wpa_ascii)
	       	eyefi_printf(" with passphrase: '%s'", wpa_ascii);
	fflush(NULL);

	// test network
	network_action('t', essid, wpa_ascii);
	u8 last_rsp = -1;

	char rsp = '\0';
	for (i=0; i < 200; i++) {
		struct byte_response *r;
		issue_noarg_command('s');
		r = eyefi_response();
		rsp = r->response;
		char *state = net_test_state_name(rsp);
		if (rsp == last_rsp) {
			eyefi_printf(".");
			fflush(NULL);;
		} else {
			if (rsp)
				eyefi_printf("\nTesting connecion to '%s' (%d): %s", essid, rsp, state);
			last_rsp = rsp;
		}
		
		if (!strcmp("success", state)) {
			ret = 0;
			break;
		}
		if (!strcmp("not scanning", state))
			break;
		if (!strcmp("unknown", state))
			break;
	}
	eyefi_printf("\n");
	if (!ret) {
		eyefi_printf("Succeeded connecting to: '%s'\n", essid);
	} else {
		eyefi_printf("Unable to connect to: '%s' (final state: %d/'%s')\n", essid,
				rsp, net_test_state_name(rsp));
	}
	return ret;
}

int print_log(void)
{
	int i;
	u8 *resbuf = malloc(EYEFI_BUF_SIZE*4);
	int total_bytes;

	total_bytes = get_log_into(resbuf);
	if (total_bytes < 0) {
		debug_printf(1, "%s() error: %d\n", __func__, total_bytes);
		return total_bytes;
	}
	// The last byte *should* be a null, and the 
	// official software does not print it.
	for (i = 0; i < total_bytes-1; i++) {
		char c = resbuf[i];
		// the official software converts UNIX to DOS-style
		// line breaks, so we'll do the same
		if (c == '\n')
			printf("%c", '\r');
		printf("%c", c);
	}
	printf("\n");
	// just some simple sanity checking to make sure what
	// we are fetching looks valid
	/* needs to be rethought for the new aligned logs
	int null_bytes_left = 20;
	if (resbuf[log_end] != 0) {
		debug_printf(2, "error: unexpected last byte (%ld/0x%lx) of log: %02x\n",
				log_end, log_end, resbuf[log_end]);
		for (i=0; i<log_size; i++) {
			if (resbuf[i])
				continue;
			if (null_bytes_left <= 0)
				continue;
			null_bytes_left--;
			debug_printf(2, "null byte %d\n", i);
		}
	}
	*/
	return 0;
}

void open_error(char *file, int ret)
{
	fprintf(stderr, "unable to open '%s' (%d)\n", file, ret);
	fprintf(stderr, "Is the Eye-Fi card inserted and mounted at: %s ?\n", locate_eyefi_mount());
	fprintf(stderr, "Do you have write permissions to it?\n");
	fprintf(stderr, "debug information:\n");
	if (eyefi_debug_level > 0)
		system("cat /proc/mounts >&2");
	if (eyefi_debug_level > 1)
		perror("bad open");
	exit(1);
}

void usage(void)
{
	printf("Usage:\n");
	printf("  eyefitest [OPTIONS]\n");
	printf("  -a ESSID	add network (implies test unless --force)\n");
	printf("  -t ESSID	test network\n");
	printf("  -p KEY	set WPA key for add/test\n");
	printf("  -r ESSID	remove network\n");
	printf("  -s		scan for networks\n");
	printf("  -c		list configured networks\n");
	printf("  -b		reboot card\n");
	printf("  -f            print information about card firmware\n");
	printf("  -d level	set debugging level (default: 1)\n");
	printf("  -k		print card unique key\n");
	printf("  -l		dump card log\n");
	printf("  -m		print card mac\n");
	exit(4);
}

int main(int argc, char **argv)
{
        int option_index;
        char c;
	int magic0 = 19790111;
	char *essid = NULL;
	char *passwd = NULL;
	int magic1 = 1111979;
	char network_action = 0;
	static int force = 0;
	static struct option long_options[] = {
		//{"wep", 'x', &passed_wep, 1},
		//{"wpa", 'y', &passed_wpa, 1},
		{"force", 0, &force, 1},
		{"help", 'h', NULL, 1},
	};

	if (argc == 1)
		usage();

	debug_printf(3, "%s starting...\n", argv[0]);

        debug_printf(3, "about to parse arguments\n");
        while ((c = getopt_long_only(argc, argv, "a:bcd:kflmp:r:st:z",
                        &long_options[0], &option_index)) != -1) {
        	debug_printf(3, "argument: '%c' %d optarg: '%s'\n", c, c, optarg);
		switch (c) {
		case 0:
			// was a long argument
			break;
		case 'a':
		case 't':
		case 'r':
			essid = strdup(optarg);
			network_action = c;
			break;
		case 'b':
			reboot_card();
			break;
		case 'c':
			print_configured_nets();
			break;
		case 'd':
			eyefi_debug_level = atoi(optarg);
			fprintf(stderr, "set debug level to: %d\n", eyefi_debug_level);
			break;
		case 'f':
			print_card_firmware_info();
			break;
		case 'k':
			print_card_key();
			break;
		case 'l':
			print_log();
			break;
		case 'm':
			print_card_mac();
			break;
		case 'p':
			passwd = strdup(optarg);
			break;
		case 's':
			scan_print_nets();
			break;
		case 'z': {
			extern void testit0(void);
			testit0();
			break;
		}
		case 'h':
		default:
			usage();
			break;
		}
	}

	debug_printf(3, "after arguments1 essid: '%s' passwd: '%s'\n", essid, passwd);
	if (network_action && essid) {
		int ret = 0;
		init_card();
		switch (network_action) {
		case 't':
			ret = try_connection_to(essid, passwd);
			break;
		case 'a':
			if (!force) {
				ret = try_connection_to(essid, passwd);
			} else {
				debug_printf(1, "forced: skipping network test\n");
			}
			if (ret) {
				printf("Error connecting to network '%s', not adding.\n", essid);
				printf("use --force to override\n");
				break;
			}
			add_network(essid, passwd);
			break;
		case 'r':
			remove_network(essid);
			break;
		}
	}

	free(essid);
	free(passwd);
	return 0;
}

