/*
g++ -O2 -Wall -DNO_FREETYPE geotag.c framegrabber.cpp -o geotag -lgps
     -lpngwriter -lpng -lsqlite3

Copyright (c) 2010 Mosalam Ebrahimi <m.ebrahimi@ieee.org>

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
*/

#include <sys/select.h>
#include <stdio.h>
#include <gps.h>
#include <pngwriter.h>
#include <sqlite3.h>
#include <signal.h>
#include "framegrabber.h"

bool isConn2GPSD = false;

struct gps_data_t *gpsdata = 0;

bool run = true;

void SigHandler(int sig) 
{
	run = false;
	printf("\nExit\n");
}

void ConnectToGPSD() 
{
	if (gpsdata == NULL) {
		char server[] = "localhost";
		char port[] = "2947";

		gpsdata = gps_open(server, port);

		if (gpsdata != NULL) {
			isConn2GPSD = true;
			gps_stream(gpsdata,
				WATCH_ENABLE|WATCH_NEWSTYLE|POLL_NONBLOCK,
				NULL);
		}
	}
}

int main()
{
	signal(SIGINT, &SigHandler);

	ConnectToGPSD();

	sqlite3 *db;
	sqlite3_open("images.db", &db);

	// vga
	const size_t width = 640;
	const size_t height = 480;
	// yuyv
	const size_t depth = 2;
	const int image_size = width * height * depth;
	// buffer for grabbed frame
	unsigned char* tmpBuff = (unsigned char*) calloc(image_size,
						 sizeof(unsigned char));
	
	// number of buffers for v4l mmap streaming
	const size_t n_buffers = 6;
	FrameGrabber cam("/dev/video1", width, height, n_buffers);
	cam.Init();
	cam.StartCapturing();

	// image number
	int idx = 0;

	// main loop
	for (;run;) {

		// busy loop: wait for a new frame
		while (cam.GrabFrame(tmpBuff) != 1);

		// check if gpsd has new data
		if (!gps_waiting(gpsdata)) 
			continue;

		// request data from gpsd
		if (gps_poll(gpsdata) != 0) {
			fprintf( stderr, "socket error\n");
			exit(2);
		}

		// hack?, skip SKY, pass TPV
		if (!(gpsdata->set & LATLON_SET)) 
			continue;
		gpsdata->set = 0;

		printf("time: %f Lat: %f Lon: %f Image: %d\n",
						gpsdata->fix.time,
						gpsdata->fix.latitude,
						gpsdata->fix.longitude,
						idx);

		// save png
		char filename[15] = "";
		sprintf(filename, "%d.png", idx);
		pngwriter png(width, height, 0, filename);
		for (size_t y=0; y<height; y++)
			for (size_t x=0; x<width; x++) {
				const int yy = y * depth;
				const int xx = x * depth;
				png.plot(x,y, 
					tmpBuff[image_size-yy*width+xx]*256,
					tmpBuff[image_size-yy*width+xx]*256,
					tmpBuff[image_size-yy*width+xx]*256);
			}
		png.close();

		// insert corresponding row into table 
		char cmnd[80];
		sprintf(cmnd, "insert into images values(%f,%f,%d);",
					gpsdata->fix.latitude,
				 	gpsdata->fix.longitude,
					idx);
		sqlite3_exec(db, cmnd, NULL, 0, NULL);

		idx++;
	}

	gps_close(gpsdata);
	cam.StopCapturing();
	cam.Uninit();
	free(tmpBuff);
	sqlite3_close(db);
	
	return 0;
}

