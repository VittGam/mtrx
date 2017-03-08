/*
 * mtrx - Transmit and receive audio via UDP unicast or multicast
 * Copyright (C) 2014-2017 Vittorio Gambaletta <openwrt@vittgam.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

extern int mtx_main(int argc, char **argv);
extern int mrx_main(int argc, char **argv);

int main(int argc, char **argv) {
	if (strstr(argv[0], "mtx")) {
		return mtx_main(argc, argv);
	} else if (strstr(argv[0], "mrx")) {
		return mrx_main(argc, argv);
	} else if (argc > 1) {
		if (strstr(argv[1], "mtx")) {
			return mtx_main(argc - 1, argv + 1);
		} else if (strstr(argv[1], "mrx")) {
			return mrx_main(argc - 1, argv + 1);
		}
	}
	fprintf(stderr, "mtrx - Transmit and receive audio via UDP unicast or multicast\n");
	fprintf(stderr, "Copyright (C) 2014-2017 Vittorio Gambaletta <openwrt@vittgam.net>\n\n");
	fprintf(stderr, "Invalid command.\n\nUsage: %s mtx|mrx [<options>]\n\n", argv[0]);
	return 127;
}
