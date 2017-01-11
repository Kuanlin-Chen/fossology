/*
 Copyright (C) 2014-2016, Siemens AG
 Author: Daniele Fognini

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along
 with this program; if not, write to the Free Software Foundation, Inc.,
 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef JSON_DATA_RETRIVER_H
#define JSON_DATA_RETRIVER_H

/*
 *
 * should return a json in this format:
 *
 * { "licenses" : [
 *                  { "name": "Apache-2.0", "text" : "licText", "files" : [ "/a.txt", "/b.txt" ]},
 *                  { "name": "Apache-1.0", "text" : "lic3Text", "files" : [ "/c.txt" ]},
 *                ]
 * }
 */
char* getClearedLicenses(int uploadId, int groupId);
char* getClearedCopyright(int uploadId);
char* getClearedIp(int uploadId);
char* getClearedEcc(int uploadId);
char* getMatches(int uploadId, int groupId);
char* getKeywords(int uploadId);
char* getIrrelevant(int uploadId, int groupId);
char* getMainLicense(int uploadId, int groupId);
char* getLicenseHistogram(int uploadId, int groupId);
char* getClearedComment(int uploadId, int groupId);
char* getClearedAcknowledgement(int uploadId, int groupId);
#endif
