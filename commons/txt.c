/*
 * Copyright (C) 2012 Sistemas Operativos - UTN FRBA. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "txt.h"

#include <stdio.h>


FILE* txt_open_for_append(char* path) {
	return fopen(path, "a");
}

void txt_write_in_file(FILE* file, char* bytes) {
	fprintf(file, "%s", bytes);
	fflush(file);
}

void txt_write_in_stdout(char* string) {
	printf("%s", string);
	fflush(stdout);
}

void txt_close_file(FILE* file) {
	fclose(file);
}

FILE* txt_open_for_read(char* path) {
	return fopen(path, "r");
}

int txt_total_lines(FILE * file){
	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	int sz = 0;

	while((read = getline(&line, &len, file)) != -1)
	{
		sz++;
	}

	fseek(file, 0L, SEEK_SET);

	free(line);

	return sz;
}


char * read_line(FILE * file, int puntero){
	char * line = NULL;
	size_t len = 0;
	ssize_t read;

	int i = 1;

	while ( (read = getline(&line, &len, file)) != -1 )
	{
		if(i==puntero)
		{
			break;
		}
		i++;


	}

	fseek(file, 0L, SEEK_SET);

	return line;
}
