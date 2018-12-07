/*
 Copyright (C) 2014-2015,2018 Siemens AG

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

#define _GNU_SOURCE
#include <string.h>

#include<sys/stat.h>
#include<sys/types.h>
#include<sys/wait.h>
#include "utils.h"
#include "table.h"
#include "jsonDataRetriever.h"

#include <time.h>
/* std library includes */
#include <unistd.h>

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
/* other library includes */
#include <libfossology.h>
#include <libfossdbmanager.h>
#include <libpq-fe.h>
#include <libfossrepo.h>


#ifdef COMMIT_HASH_S
char BuildVersion[] = "reportgen build version: " VERSION_S " r(" COMMIT_HASH_S ").\n";
#else
char BuildVersion[] = "reportgen build version: NULL.\n";
#endif

#include "main.h"

const char* dirs[] = {"docProps/", "_rels/", "word/", "word/_rels/"};

#define CLEARINGREPMID "_clearing_report_"

PGconn* pgConn; // the connection to Database
fo_dbManager* dbManager; // the Database Manager

char* destFolder()
{
  char *destPath="/localhost/files/report";
  return g_strdup_printf("%s%s", fo_RepGetRepPath(), destPath);
}

char* createzipname(char* pckgname) {
  char* zipname = NULL;
  if (zipname == NULL) {
    zipname = (char*) malloc(sizeof (char)*(strlen(pckgname) + strlen(".zip") + 1));
    strcpy(zipname, pckgname);
    strcat(zipname, ".zip");
  }
  return zipname;
}

char* createdocxname(char* pckgname) {
  char* docxname = NULL;
  if (docxname == NULL) {
    docxname = (char*) malloc(sizeof (char)*(strlen(pckgname) + strlen(".docx") + 1));
    strcpy(docxname, pckgname);
    strcat(docxname, ".docx");
  }
  return docxname;
}

char* gettargetdir(char* pckgname) {
  return g_strdup_printf("%s/%s", destFolder(), pckgname);
}

int zipdir(char* name) {
  pid_t child_pid;
  int status;
  char* cmd[6];
  char* targetdir = NULL;
  char* zipcmd = "/usr/bin/zip";
  char* docxfilename = NULL;
  char* zipname = createzipname(name);
  char* docxfullpath = NULL;
  targetdir = gettargetdir(name);
  docxfilename = createdocxname(name);
  docxfullpath = g_strdup_printf("%s/%s", targetdir, docxfilename);
  cmd[0] = g_strdup("/usr/bin/zip");
  cmd[1] = g_strdup("-r");
  cmd[2] = g_strdup("-q");
  cmd[3] = g_strdup_printf("%s/%s", targetdir, zipname);
  char* fullzipname = cmd[3];
  cmd[4] = g_strdup(".");
  cmd[5] = NULL;
  if ((child_pid = fork()) < 0) {
    perror("fork failure");
    exit(1);
  }
  if (child_pid == 0) {
    char* chdir_cmd = g_strdup_printf("%s/%s", targetdir, name);
    if (chdir_cmd && chdir(chdir_cmd) != -1) {
      g_free(chdir_cmd);
    }
    else {
      exit(1);
    }

    if (execv(zipcmd, cmd)) {
      perror("calling zip failed");
      printf("cmd='%s'\n", zipcmd);
      int ip = 0;
      char** cmdI = cmd;
      while (*cmdI) {
        printf("argv[%d]='%s'\n", ip, *cmdI);
        ip++;
        cmdI++;
      }
      exit(5);
    }
  }
  else {
    wait(&status);
    if (rename(fullzipname, docxfullpath) < 0) {
      printf("failed to rename %s to %s\n", fullzipname, docxfullpath);
      fo_scheduler_disconnect(6);
      exit(6);
    }
  }
  char** cmdI = cmd;
  while (*cmdI)
    g_free(*(cmdI++));

  if (docxfullpath) {
    g_free(docxfullpath);
  }
  if (docxfilename) {
    g_free(docxfilename);
  }
  if (targetdir) {
    g_free(targetdir);
  }
  return 0;
}

int createdir(char* path) {
  char CMD[500];
  DIR* dir = NULL;

  snprintf(CMD, 499, "mkdir -p -m 777 '%s' >/dev/null 2>&1", path);
  dir = opendir(path);
  if (dir) {
    closedir(dir);
  }
  else {
    if (system(CMD) == -1) {
      return 0;
    }
  }

  return 1;
}

int createdirinner(char* path) {
  int i = 0;
  char* innerdir = NULL;
  createdir(path);
  for (i = 0; i < 4; i++) {
    innerdir = g_strdup_printf("%s/%s", path, dirs[i]);
    if (innerdir) {
      createdir(innerdir);
      g_free(innerdir);
    }
    else {
      return 0;
    }
  }
  return 1;
}

int checkdest() {
  char* CMD = g_strdup_printf("mkdir -p -m 777 %s >/dev/null 2>&1", destFolder());
  DIR* dir = NULL;
  dir = opendir(destFolder());
  if (dir) {
    closedir(dir);
  }
  else {
    if (system(CMD) == -1) {
      return 0;
    }
  }
  return 1;
}

int createdocxstructure(char* pckgname) {
  gchar* dest = g_strdup_printf("%s/%s/%s/", destFolder(), pckgname, pckgname);

  int result = 0;
  if (dest) {
    result = createdirinner(dest);
    g_free(dest);
  }

  return result;
}

char* replaceunderscore(char* systime) {
  int i = 0;
  char* formattedtime = NULL;
  char* ptr = NULL;
  formattedtime = (char*) malloc(sizeof (char)*(strlen(systime) + 1 + 1));
  int hyphencount = 0;
  if (formattedtime) {
    ptr = systime;
    while (*ptr != '\0') {
      if (!isspace(*ptr) && (*ptr != ':') && (*ptr != ' ')) {
        formattedtime[i] = *ptr;
        i++;
      }
      else {
        hyphencount++;
        if (hyphencount == 3 || hyphencount == 6) {
          formattedtime[i] = '-';
          i++;
        }
        if (hyphencount == 4 || hyphencount == 5) {
          formattedtime[i] = '_';
          i++;
        }
      }
      ptr++;
    }
    formattedtime[i] = '\0';
  }
  else {
    return NULL;
  }
  return formattedtime;
}

char* gettime() {
  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  return asctime(timeinfo);
}

gchar* getUsrGrpName(fo_dbManager* dbManager) {
  gchar* usrGrpName = NULL;
  PGresult* usrGrpNameRes = fo_dbManager_Exec_printf(
          dbManager,
          "SELECT user_name, group_name FROM users,groups WHERE user_pk = %d AND group_pk = %d",
          fo_scheduler_userID(), fo_scheduler_groupID()
          );

  if (usrGrpNameRes) {
    if (PQntuples(usrGrpNameRes) > 0) {
      usrGrpName = g_strdup_printf("%s (%s)", PQgetvalue(usrGrpNameRes, 0, 0), PQgetvalue(usrGrpNameRes, 0, 1));
    }
    PQclear(usrGrpNameRes);
  }

  if (!usrGrpName)
    usrGrpName = g_strdup("[unknown]");

  return usrGrpName;
}

char* getParentFromUploadtreePk(long uploadtree_pk) {
  char* parent = NULL;
  PGresult* QryRes = fo_dbManager_ExecPrepared(
          fo_dbManager_PrepareStamement(
          dbManager,
          "getParentFromUploadtreePk",
          "select parent from uploadtree where uploadtree_pk=$1",
          long),
          uploadtree_pk
          );
  if (QryRes) {
    if (PQntuples(QryRes) > 0)
      parent = g_strdup(PQgetvalue(QryRes, 0, 0));
    PQclear(QryRes);
  }
  return parent;
}

/*
 * containers need to be written only if they are not the uppermost entry
 *
 * e.g. the tree
 *   file -> [artifact] -> [container] p.zip -> folder1 -> [artifact] -> [container] up.tar
 *
 * becomes
 *
 *   /folder1/p.zip/file
 *
 */
char* getFullFilePath(long uploadTreeId) {
  GString* pathBuilder = g_string_new("");
  GString* containers = g_string_new("");

  long parent = uploadTreeId;

#define isContainer(mode) (((mode) & 1<<29) != 0)
#define isArtifact(mode)  (((mode) & 1<<28) != 0)

  do {
    PGresult* parentAndNameRes = fo_dbManager_ExecPrepared(
            fo_dbManager_PrepareStamement(
            dbManager,
            "getFullFilePath",
            "SELECT parent,ufile_name,ufile_mode FROM uploadtree WHERE uploadtree_pk=$1",
            long
            ),
            parent
            );

    parent = 0;
    if (parentAndNameRes) {
      if (PQntuples(parentAndNameRes) > 0) {
        parent = atol(PQgetvalue(parentAndNameRes, 0, 0));

        char* currentName = PQgetvalue(parentAndNameRes, 0, 1);
        long fileMode = atol(PQgetvalue(parentAndNameRes, 0, 2));

        if (isArtifact(fileMode)) {
          g_string_prepend(pathBuilder, g_string_free(containers, FALSE));
          containers = g_string_new("");
        }
        else if (isContainer(fileMode)) {
          g_string_prepend(containers, currentName);
          g_string_prepend(containers, "/");
        }
        else {
          g_string_prepend(pathBuilder, currentName);
          g_string_prepend(pathBuilder, "/");
        }
      }
      PQclear(parentAndNameRes);
    }
  } while (parent > 0);
  g_string_free(containers, TRUE);

  return g_string_free(pathBuilder, FALSE);
}

char* implodeJsonArray(json_object* jsonArray, const char* delimiter) {
  GString* fileNamesAppender = g_string_new("");
  int lenght = json_object_array_length(jsonArray);
  for (int jf = 0; jf < lenght; jf++) {
    json_object* value = json_object_array_get_idx(jsonArray, jf);
    if (value == NULL) {
      printf("unexpected NULL in json object\n");
      return NULL;
    }
    if (json_object_is_type(value, json_type_string)) {
      const char* file = json_object_get_string(value);
      if (jf > 0)
        g_string_append(fileNamesAppender, delimiter);
      g_string_append(fileNamesAppender, file);
    }
  }
  return g_string_free(fileNamesAppender, FALSE);
}

int addRowsFromJson_ContentTextFiles(rg_table* table, json_object* jobj, const char* keyName) {
  if ((jobj == NULL) || is_error(jobj))
  {
    printf("json object is ill-formatted\n");
    return 0;
  }

  int result = 0;

  if (!json_object_is_type(jobj, json_type_object)) {
    printf("expected object but type is %d\n", json_object_get_type(jobj));
    return 0;
  }

  json_object_object_foreach(jobj, key, val) {
    if ((strcmp(keyName, key) == 0) && json_object_is_type(val, json_type_array)) {
      int length = json_object_array_length(val);

      for (int j = 0; j < length; j++) {
        json_object* val1 = json_object_array_get_idx(val, j);
        if (!json_object_is_type(val1, json_type_object)) {
          printf("wrong type for index %d in '%s'\n", j, key);
          return 0;
        }
        const char* content = NULL;
        const char* text = NULL;
        const char* acknowledgement = NULL;
        const char* fileName = NULL;
        char* fileNames = NULL;
        char* licenses = NULL;

        json_object_object_foreach(val1, key2, val2) {
          if (((strcmp(key2, "content")) == 0) && json_object_is_type(val2, json_type_string)) {
            content = json_object_get_string(val2);
          }
          else if (((strcmp(key2, "text")) == 0) && json_object_is_type(val2, json_type_string)) {
            text = json_object_get_string(val2);
          }
          else if (((strcmp(key2, "acknowledgement")) == 0) && json_object_is_type(val2, json_type_string)) {
            acknowledgement = json_object_get_string(val2);
          }
          else if (((strcmp(key2, "fileName")) == 0) && json_object_is_type(val2, json_type_string)) {
            fileName = json_object_get_string(val2);
          }
          else if (((strcmp(key2, "files")) == 0) && json_object_is_type(val2, json_type_array)) {
            fileNames = implodeJsonArray(val2, ",\n");
          }
          else if (((strcmp(key2, "licenses")) == 0) && json_object_is_type(val2, json_type_array)) {
            licenses = implodeJsonArray(val2, ",\n");
          }
          else if (((strcmp(key2, "hash")) == 0) && json_object_is_type(val2, json_type_array)) {
            fileNames = implodeJsonArray(val2, ",\n");
          }
          else {
            printf("unexpected key/typeof(value) pair for key '%s'\n", key2);
            return 0;
          }
        }

        if(content && text && acknowledgement && fileNames){
          table_addRow(table, content, text, acknowledgement, fileNames);
	}
	else if (content && text && fileNames) {
          table_addRow(table, content, text, fileNames);
        }
	else if (content && fileName && licenses) {
          table_addRow(table, content, fileName, licenses);
        }
        //else if (content && comments && fileNames){
        //  table_addRow(table, content, comments, fileNames);
       // }
        else if (content && fileNames) { // TODO use a count argument in addRowsFromJson_ContentTextFiles
          table_addRow(table, content, fileNames);
        }
        else if (content && text) {
          table_addRow(table, content, text);
        }

        if (fileNames) g_free(fileNames);
      }
      result = 1;
    }
  }

  return result;
}

int addRowsFromJson_Histogram(rg_table* table, json_object* jobj, const char* keyName) {
  if ((jobj == NULL) || is_error(jobj))
  {
    printf("json object is ill-formatted\n");
    return 0;
  }

  int result = 0;

  if (!json_object_is_type(jobj, json_type_object)) {
    printf("expected object but type is %d\n", json_object_get_type(jobj));
    return 0;
  }

  json_object_object_foreach(jobj, key, val) {
    if ((strcmp(keyName, key) == 0) && json_object_is_type(val, json_type_array)) {
      int length = json_object_array_length(val);

      for (int j = 0; j < length; j++) {
        json_object* val1 = json_object_array_get_idx(val, j);
        if (!json_object_is_type(val1, json_type_object)) {
          printf("wrong type for index %d in '%s'\n", j, key);
          return 0;
        }
        const char*  scannerCount = NULL;
        const char*  editedCount = NULL;
        const char* licenseShortname = NULL;

        json_object_object_foreach(val1, key2, val2) {
          if (((strcmp(key2, "scannerCount")) == 0) && (json_object_is_type(val2, json_type_int) || json_object_is_type(val2, json_type_string))) {
            scannerCount = json_object_get_string(val2);
          }
          else if (((strcmp(key2, "editedCount")) == 0) && (json_object_is_type(val2, json_type_int) || json_object_is_type(val2, json_type_string))) {
            editedCount = json_object_get_string(val2);
          }
          else if (((strcmp(key2, "licenseShortname")) == 0) && json_object_is_type(val2, json_type_string)) {
            licenseShortname = json_object_get_string(val2);
          }
          else {
            printf("unexpected key/typeof(value) pair for key '%s'\n", key2);
            return 0;
          }
        }
        table_addRow(table, scannerCount, editedCount, licenseShortname);
      }
      result = 1;
    }
  }

  return result;
}

void fillTableFromJson(rg_table* table, const char* json) {
  json_object* jobj = json_tokener_parse(json);

  if (!addRowsFromJson_ContentTextFiles(table, jobj, "statements")) {
    printf("cannot parse json string: %s\n", json);
    fo_scheduler_disconnect(1);
    exit(1);
  }

  json_object_put(jobj);
}

int main(int argc, char** argv) {
  FILE *fp1;
  FILE *fp;
  FILE *hdrf;
  FILE *fdrf;
  FILE *ref;
  FILE *ffont;
  FILE *fstyle;
  FILE *fnumbering;
  FILE *fcontents;
  FILE *fapp;
  FILE *fcore;
  FILE *fhiddenrels;
  mxml_node_t *xml = NULL;
  mxml_node_t *document;
  mxml_node_t *header;
  mxml_node_t *footer;
  mxml_node_t *refhandle;
  mxml_node_t *fonthandle;
  mxml_node_t *stylehandle;
  mxml_node_t *numberinghandle;
  mxml_node_t *contenthandle;
  mxml_node_t *contentxml;
  mxml_node_t *apphandle;
  mxml_node_t *appxml;
  mxml_node_t *corehandle;
  mxml_node_t *corexml;
  mxml_node_t *hiddenrelshandle;
  mxml_node_t *hiddenrelsxml;
  mxml_node_t *hxml;
  mxml_node_t *fxml;
  mxml_node_t *refxml;
  mxml_node_t *body;
  mxml_node_t *fontxml;
  mxml_node_t *stylexml;
  mxml_node_t *numberingxml;
  char* tbcol4[4];
  char* tbcol3[3];
  char* tbcolSkewed[3];

  char* section1 = NULL;
  char* finaldocxpath = NULL;

  char* outputPkgName = NULL;

  char agent_rev[myBUFSIZ];
  char *agent_desc = "reportgen agent";
  char *COMMIT_HASH = NULL;
  char *VERSION = NULL;
  long agent_pk = 0;
  int user_pk = 0;
  int uploadId;
  int ars_pk = 0;
  /* connect to the scheduler */

  fo_scheduler_connect(&argc, argv, &pgConn);
  dbManager = fo_dbManager_new(pgConn);

  COMMIT_HASH = fo_sysconfig("reportgen", "COMMIT_HASH");
  VERSION = fo_sysconfig("reportgen", "VERSION");
  sprintf(agent_rev, "%s.%s", VERSION, COMMIT_HASH);

  agent_pk = fo_GetAgentKey(pgConn, AGENT_NAME, 0, agent_rev, agent_desc);
  user_pk = fo_scheduler_userID();
  int groupId = fo_scheduler_groupID();
  while (fo_scheduler_next() != NULL) {
    uploadId = atoi(fo_scheduler_current());
    ars_pk = fo_WriteARS(pgConn, 0, uploadId, agent_pk, AGENT_ARS, NULL, 0);

    /*Check Permissions */
    if (GetUploadPerm(pgConn, uploadId, user_pk) < PERM_WRITE) {
      continue;
    }
    checkdest();
    umask(022);
    char* localtime1 = gettime();
    char* formattedtime = replaceunderscore(localtime1);

    PGresult* uploadNameRes = fo_dbManager_Exec_printf(dbManager, "select ufile_name from uploadtree where upload_fk=%d and parent is null", uploadId);
    if (uploadNameRes) {
      if (PQntuples(uploadNameRes) > 0) {
        outputPkgName = g_strdup_printf("%s%s%s", PQgetvalue(uploadNameRes, 0, 0), CLEARINGREPMID, formattedtime);
      }
      PQclear(uploadNameRes);
    }

    if (!outputPkgName) {
      printf("FATAL: cannot determine a name for upload=%d\n", uploadId);
      fo_scheduler_disconnect(1);
      exit(1);
    }
    
    createdocxstructure(outputPkgName);
    //------------Below: Code to create XML files---------//
    char* fullpathWithoutSlash = gettargetdir(outputPkgName);
    char* fullpath = (char*) malloc(sizeof (char)*(strlen(fullpathWithoutSlash) + 2 + strlen(outputPkgName) + 1));
    sprintf(fullpath, "%s/%s/", fullpathWithoutSlash, outputPkgName);
    finaldocxpath = (char*) malloc(sizeof (char)*(strlen(fullpath) + strlen(outputPkgName) + strlen(".docx") + 1));
    sprintf(finaldocxpath, "%s/%s.docx", fullpathWithoutSlash, outputPkgName);
    char* filexmlpath = (char*) malloc(sizeof (char)*(strlen(fullpath) + strlen("word/") + 1));
    strcpy(filexmlpath, fullpath);
    strcat(filexmlpath, "word/");

    char* resxmlpath = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen("_rels/") + 1));
    strcpy(resxmlpath, filexmlpath);
    strcat(resxmlpath, "_rels/");

    char* docpropspath = (char*) malloc(sizeof (char)*(strlen(fullpath) + strlen("docProps/") + 1));
    strcpy(docpropspath, fullpath);
    strcat(docpropspath, "docProps/");

    char* hiddenrelspath = (char*) malloc(sizeof (char)*(strlen(fullpath) + strlen("_rels/") + 1));
    strcpy(hiddenrelspath, fullpath);
    strcat(hiddenrelspath, "_rels/");

    tbcol4[0] = (char*) malloc(sizeof (char) *(strlen("3210") + 1));
    strcpy(tbcol4[0], "2409");
    tbcol4[1] = (char*) malloc(sizeof (char) *(strlen("3210") + 1));
    strcpy(tbcol4[1], "2409");
    tbcol4[2] = (char*) malloc(sizeof (char) *(strlen("3218") + 1));
    strcpy(tbcol4[2], "2409");
    tbcol4[3] = (char*) malloc(sizeof (char) *(strlen("3218") + 1));
    strcpy(tbcol4[3], "2411");

    tbcol3[0] = (char*) malloc(sizeof (char) *(strlen("3210") + 1));
    strcpy(tbcol3[0], "3210");
    tbcol3[1] = (char*) malloc(sizeof (char) *(strlen("3210") + 1));
    strcpy(tbcol3[1], "3210");
    tbcol3[2] = (char*) malloc(sizeof (char) *(strlen("3218") + 1));
    strcpy(tbcol3[2], "3218");

    tbcolSkewed[0] = (char*) malloc(sizeof (char) *(strlen("1000") + 1));
    strcpy(tbcolSkewed[0], "1000");
    tbcolSkewed[1] = (char*) malloc(sizeof (char) *(strlen("6000") + 1));
    strcpy(tbcolSkewed[1], "6000");
    tbcolSkewed[2] = (char*) malloc(sizeof (char) *(strlen("1000") + 1));
    strcpy(tbcolSkewed[2], "1500");

    char* tbcol6[3];
    tbcol6[0] = (char*) malloc(sizeof (char) *(strlen("1000") + 1));
    strcpy(tbcol6[0], "1000");
    tbcol6[1] = (char*) malloc(sizeof (char) *(strlen("6000") + 1));
    strcpy(tbcol6[1], "6000");
    tbcol6[2] = (char*) malloc(sizeof (char) *(strlen("2000") + 1));
    strcpy(tbcol6[2], "2000");

    xml = mxmlNewXML("1.0");
    hxml = mxmlNewXML("1.0");
    fxml = mxmlNewXML("1.0");
    refxml = mxmlNewXML("1.0");
    fontxml = mxmlNewXML("1.0");
    stylexml = mxmlNewXML("1.0");
    numberingxml = mxmlNewXML("1.0");
    contentxml = mxmlNewXML("1.0");
    appxml = mxmlNewXML("1.0");
    corexml = mxmlNewXML("1.0");
    hiddenrelsxml = mxmlNewXML("1.0");
    header = (mxml_node_t*) createheader(hxml);
    footer = (mxml_node_t*) createfooter(fxml);
    refhandle = (mxml_node_t*) createreference(refxml);
    document = (mxml_node_t*) createbodyheader(xml);
    fonthandle = (mxml_node_t*) createfont(fontxml);
    stylehandle = (mxml_node_t*) createstyle(stylexml);
    numberinghandle = (mxml_node_t*) createnum(numberingxml);
    contenthandle = (mxml_node_t*) createcontent(contentxml);
    apphandle = (mxml_node_t*) createappxml(appxml);
    corehandle = (mxml_node_t*) createcorexml(corexml);
    hiddenrelshandle = (mxml_node_t*) createrelxml(hiddenrelsxml);
    body = mxmlNewElement(document, "w:body");

    addheading(body, "Component Clearing Report for 3rd Party SW - V1");

    //add numbered section

    mxml_node_t* p1 = (mxml_node_t*) createnumsection(body, "0", "2");

    char* formattedDate = malloc(strlen(formattedtime) + 4 + 1 + 1);

    char* day1 = malloc(4);
    if (day1) {
      strncpy(day1, formattedtime, 3);
      day1[3] = '\0';
      sprintf(formattedDate, "%s %s", day1, formattedtime + 3);
      free(day1);
    }

    char* usrGrpName = getUsrGrpName(dbManager);

    section1 = g_strdup_printf("%s by %s", formattedDate, usrGrpName);

    if (section1) {
      addparaheading(p1, NULL, section1, "0", "2");
      free(section1);
    }
    if (usrGrpName)
      free(usrGrpName);
    if (formattedDate)
      free(formattedDate);

    {
      char* uploadName = "[upload name not found]";

      PGresult* uploadNameRes = fo_dbManager_Exec_printf(dbManager, "select ufile_name from uploadtree where upload_fk=%d and parent is null", uploadId);
      if (uploadNameRes && (PQntuples(uploadNameRes) > 0))
        uploadName = PQgetvalue(uploadNameRes, 0, 0);

      mxml_node_t* p2 = (mxml_node_t*) createnumsection(body, "0", "2");
      addparaheading(p2, NULL, uploadName, "0", "2");

      if (uploadNameRes)
        PQclear(uploadNameRes);
    }

    mxml_node_t* p3 = (mxml_node_t*) createnumsection(body, "0", "2");
    addparaheading(p3, NULL, "When using this component, you need to fulfil the \"ToDos\"", "0", "2");
    mxml_node_t* p4 = (mxml_node_t*) createnumsection(body, "0", "2");
    addparaheading(p4, NULL, "Functionality", "0", "2");
    addparagraph(body, NULL, "Most recent stable version at clearing date and date of version requested for clearing:");
    //table 1 at sec 4
    mxml_node_t* tbl1 = (mxml_node_t*) createtable(body, "9638");
    createtablegrid(tbl1, tbcol4, 4);
    mxml_node_t* tr1 = (mxml_node_t*) createrowproperty(tbl1);
    createrowdata(tr1, tbcol4[0], "");
    createrowdata(tr1, tbcol4[1], "Version");
    createrowdata(tr1, tbcol4[2], "Date");
    createrowdata(tr1, tbcol4[3], "SourceURL");

    mxml_node_t* tr2 = (mxml_node_t*) createrowproperty(tbl1);
    createrowdata(tr2, tbcol4[0], "");
    createrowdata(tr2, tbcol4[1], "");
    createrowdata(tr2, tbcol4[2], "");
    createrowdata(tr2, tbcol4[3], "");

    int uloop;
    for (uloop = 0; uloop < 4; uloop++) {
      if (tbcol4[uloop])
        free(tbcol4[uloop]);
    }

    /*
     * Nomos decided licenses should appear in the report
     */
    mxml_node_t* p5 = createnumsection(body, "0", "2");
    addparaheading(p5, NULL, "Results of License Scan", "0", "2");
    rg_table* tableLicenseHistogram = table_new(body, 3, "1638", "3000", "5000");

    table_addRow(tableLicenseHistogram,"Scanner Count", "Concluded License Count", "License Name");
    {
      char* jsonHistogram = getLicenseHistogram(uploadId, groupId);
      json_object * jobjt = json_tokener_parse(jsonHistogram);

      if (!addRowsFromJson_Histogram(tableLicenseHistogram, jobjt, "statements")) {
        printf("cannot parse json string: %s\n", jsonHistogram);
        fo_scheduler_disconnect(1);
        exit(1);
      }

      json_object_put(jobjt);
      g_free(jsonHistogram);
    }
    
    addparaheading(createnumsection(body, "0", "2"), NULL, "Main Licenses", "0", "2");

    rg_table* tableMainLicense = table_new(body, 2, "2000", "7638");
    table_addRow(tableMainLicense, "License", "Text");
    {
      char* jsonMainLicense = getMainLicense(uploadId, groupId);
      json_object * jobj = json_tokener_parse(jsonMainLicense);

      if (!addRowsFromJson_ContentTextFiles(tableMainLicense, jobj, "statements")) {
        printf("cannot parse json string: %s\n", jsonMainLicense);
        fo_scheduler_disconnect(1);
        exit(1);
      }

      json_object_put(jobj);
      g_free(jsonMainLicense);
    }
    
    addparaheading(createnumsection(body, "0", "2"), NULL, "Other Licenses - DO NOT USE", "0", "2");
    addparaheading(createnumsection(body, "0", "2"), NULL, "Other Licenses", "0", "2");

    //table 3 for other license data
    rg_table* tableOthers = table_new(body, 3, "2000", "4000", "3638");
    table_addRow(tableOthers, "license", "text", "files");
    {
      char* json = getClearedLicenses(uploadId, groupId);
      fillTableFromJson(tableOthers, json);
      g_free(json);
    }

    addparaheading(createnumsection(body, "0", "2"), NULL, "Acknowledgements", "0", "2");

    rg_table* tableacknowledgements = table_new(body, 3, "2000", "4000", "3638");
    table_addRow(tableacknowledgements, "Reference to the license", "Text of acknowledgements", "File path");
    {
      char* jsonAcknowledgementLicense = getClearedAcknowledgement(uploadId, groupId);
      fillTableFromJson(tableacknowledgements, jsonAcknowledgementLicense);
      g_free(jsonAcknowledgementLicense);
    }
    // endrow
    addparaheading(createnumsection(body, "0", "2"), NULL, "Copyrights", "0", "2");

    rg_table* tableCopyright = table_new(body, 3, "2638", "5000", "2000");
    table_addRow(tableCopyright, "copyright", "text", "files");
    {
      char* json = getClearedCopyright(uploadId);
      fillTableFromJson(tableCopyright, json);
      g_free(json);
    }

    addparaheading(createnumsection(body, "0", "2"), NULL, "Bulk Findings", "0", "2");

    rg_table* tableMatches = table_new(body, 3, "2000", "4000", "3638");
    table_addRow(tableMatches, "license", "text", "files");
    {
      char* json = getMatches(uploadId, groupId);
      fillTableFromJson(tableMatches, json);
      g_free(json);
    }

#ifdef REPORT_KEYWORDS
    addparaheading(createnumsection(body, "0", "2"), NULL, "Keywords", "0", "2");

    rg_table* tableKeywords = table_new(body, 3, "2000", "4000", "3638");
    table_addRow(tableKeywords, "keyword", "text", "files");
    {
      char* json = getKeywords(uploadId);
      fillTableFromJson(tableKeywords, json);
      g_free(json);
    }
#endif

    addparaheading(createnumsection(body, "0", "2"), NULL, "Special considerations", "0", "2");
    addparaheading(createnumsection(body, "1", "2"), NULL, "Known Security Vulnerabilities:", "1", "2");
    //table 5 for known security vulnerabilities
    rg_table* tableSecurity = table_new(body, 3, "3000", "3000", "3000");
    table_addRow(tableSecurity, "", "", ""); //TODO why?

    addparaheading(createnumsection(body, "1", "2"), NULL, "Known Patent Issues", "1", "2");

    rg_table* tableIP = table_new(body, 3, "3000", "3000", "3000");
    table_addRow(tableIP, "Identified Patent", "comments", "files");
    {
      char* jsonIp = getClearedIp(uploadId);
      json_object * jobj = json_tokener_parse(jsonIp);

      if (!addRowsFromJson_ContentTextFiles(tableIP, jobj, "statements")) {
        printf("cannot parse json string: %s\n", jsonIp);
        fo_scheduler_disconnect(1);
        exit(1);
      }

      json_object_put(jobj);
      g_free(jsonIp);
    }

    addparaheading(createnumsection(body, "1", "2"), NULL, "Known ECC Issues", "1", "2");

    rg_table* tableEcc = table_new(body, 3, "3000", "3000", "3000");
    table_addRow(tableEcc, "Identified Ecc Issue", "comments", "files");
    {
      char* jsonEcc = getClearedEcc(uploadId);
      json_object * jobj = json_tokener_parse(jsonEcc);

      if (!addRowsFromJson_ContentTextFiles(tableEcc, jobj, "statements")) {
        printf("cannot parse json string: %s\n", jsonEcc);
        fo_scheduler_disconnect(1);
        exit(1);
      }

      json_object_put(jobj);
      g_free(jsonEcc);
    }

    mxml_node_t* pirrelevant = (mxml_node_t*) createnumsection(body, "0", "2");
    addparaheading(pirrelevant, NULL, "Irrelevant files", "0", "2");
    
    rg_table* tableIrrelevant = table_new(body, 3, "3000", "3000", "3000");
    table_addRow(tableIrrelevant, "Path", "files", "licenses");
    {
      char* jsonIrrelevant = getIrrelevant(uploadId, groupId);
      json_object * jobj = json_tokener_parse(jsonIrrelevant);

      if (!addRowsFromJson_ContentTextFiles(tableIrrelevant, jobj, "statements")) {
        printf("cannot parse json string: %s\n", jsonIrrelevant);
        fo_scheduler_disconnect(1);
        exit(1);
      }

      json_object_put(jobj);
      g_free(jsonIrrelevant);
    }
    
    mxml_node_t* p11 = (mxml_node_t*) createnumsection(body, "0", "2");
    addparaheading(p11, NULL, "ToDos", "0", "2");

    mxml_node_t* p111 = (mxml_node_t*) createnumsection(body, "1", "2");
    addparaheading(p111, NULL, "Readme_OSS", "1", "2");

    mxml_node_t* p11b = (mxml_node_t*) createnumsection(body, "2", "2");
    addparaheading(p11b, NULL, "Add all copyrights to README_OSS", "2", "2");

    mxml_node_t* p11b1 = (mxml_node_t*) createnumsection(body, "2", "2");
    addparaheading(p11b1, NULL, "All license (global and others - see above) including copyright notice and disclaimer of warranty must be added to the README_OSS file", "2", "2");

    mxml_node_t* p112 = (mxml_node_t*) createnumsection(body, "1", "2");
    addparaheading(p112, NULL, "Obligations", "1", "2");

    mxml_node_t* p113 = (mxml_node_t*) createnumsection(body, "1", "2");
    addparaheading(p113, NULL, "Technical or other obligations", "1", "2");

    mxml_node_t* p12 = (mxml_node_t*) createnumsection(body, "0", "2");
    addparaheading(p12, NULL, "Notes", "0", "2");
    
    rg_table* tableNotes = table_new(body, 3, "2000", "3000", "2000");
    table_addRow(tableNotes, "Identified License", "Comment Entered", "File with Path");
    {
      char* jsonCommentLicense = getClearedComment(uploadId, groupId);
      json_object * jobj = json_tokener_parse(jsonCommentLicense);

      if (!addRowsFromJson_ContentTextFiles(tableNotes, jobj, "statements")) {
        printf("cannot parse json string: %s\n", jsonCommentLicense);
        fo_scheduler_disconnect(1);
        exit(1);
      }

      json_object_put(jobj);
      g_free(jsonCommentLicense);
    }
    //finish adding comments

    mxml_node_t* p13 = (mxml_node_t*) createnumsection(body, "0", "2");
    addparaheading(p13, NULL, "Changes to Clearing Protocol V1", "0", "2");
    addparagraph(body, "I", "First version and no changes to report");

    createsectionptr(body);

    char* xmlfilename1 = (char*) malloc(sizeof (char)*(strlen(fullpath) + strlen("/word/document.xml") + 1));
    sprintf(xmlfilename1, "%s/word/document.xml", fullpath);
    fp1 = fopen(xmlfilename1, "w");

    mxmlSaveFile(xml, fp1, MXML_NO_CALLBACK);
    fclose(fp1);
    if (xmlfilename1)
      free(xmlfilename1);

    char* xmlfilename = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen(HEADERXML) + 1));
    strcpy(xmlfilename, filexmlpath);
    strcat(xmlfilename, HEADERXML);
    FILE* F = fopen(xmlfilename, "w");
    fclose(F);

    hdrf = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen(FOOTERXML) + 1));
    strcpy(xmlfilename, filexmlpath);
    strcat(xmlfilename, FOOTERXML);
    fdrf = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen(FONTTABLE) + 1));
    strcpy(xmlfilename, filexmlpath);
    strcat(xmlfilename, FONTTABLE);
    ffont = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen(DOCUMENTXML) + 1));
    strcpy(xmlfilename, filexmlpath);
    strcat(xmlfilename, DOCUMENTXML);
    fp = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen(STYLESXML) + 1));
    strcpy(xmlfilename, filexmlpath);
    strcat(xmlfilename, STYLESXML);
    fstyle = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(filexmlpath) + strlen(NUMBERINGXML) + 1));
    strcpy(xmlfilename, filexmlpath);
    strcat(xmlfilename, NUMBERINGXML);
    fnumbering = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(resxmlpath) + strlen(DOCXMLRELS) + 1));
    strcpy(xmlfilename, resxmlpath);
    strcat(xmlfilename, DOCXMLRELS);
    ref = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(fullpath) + strlen(CONTENTTYPEXML) + 1));
    strcpy(xmlfilename, fullpath);
    strcat(xmlfilename, CONTENTTYPEXML);
    fcontents = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(docpropspath) + strlen(APPXML) + 1));
    strcpy(xmlfilename, docpropspath);
    strcat(xmlfilename, APPXML);
    fapp = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = (char*) malloc(sizeof (char)*(strlen(docpropspath) + strlen(COREXML) + 1));
    strcpy(xmlfilename, docpropspath);
    strcat(xmlfilename, COREXML);
    fcore = fopen(xmlfilename, "w");
    free(xmlfilename);

    xmlfilename = malloc(strlen(hiddenrelspath) + strlen(HIDDENRELSXML) + 1);
    strcpy(xmlfilename, hiddenrelspath);
    strcat(xmlfilename, HIDDENRELSXML);
    fhiddenrels = fopen(xmlfilename, "w");
    free(xmlfilename);

    mxmlSaveFile(header, hdrf, MXML_NO_CALLBACK);
    mxmlSaveFile(footer, fdrf, MXML_NO_CALLBACK);
    mxmlSaveFile(fonthandle, ffont, MXML_NO_CALLBACK);
    mxmlSaveFile(xml, fp, MXML_NO_CALLBACK);
    mxmlSaveFile(refhandle, ref, MXML_NO_CALLBACK);
    mxmlSaveFile(stylehandle, fstyle, MXML_NO_CALLBACK);
    mxmlSaveFile(numberinghandle, fnumbering, MXML_NO_CALLBACK);
    mxmlSaveFile(contenthandle, fcontents, MXML_NO_CALLBACK);
    mxmlSaveFile(apphandle, fapp, MXML_NO_CALLBACK);
    mxmlSaveFile(corehandle, fcore, MXML_NO_CALLBACK);
    mxmlSaveFile(hiddenrelshandle, fhiddenrels, MXML_NO_CALLBACK);

    fclose(fp);
    fclose(hdrf);
    fclose(fdrf);
    fclose(ref);
    fclose(ffont);
    fclose(fstyle);
    fclose(fnumbering);
    fclose(fcontents);
    fclose(fapp);
    fclose(fcore);
    fclose(fhiddenrels);
    free(filexmlpath);
    free(resxmlpath);
    free(docpropspath);
    free(hiddenrelspath);
    zipdir(outputPkgName);

    PQclear(
            fo_dbManager_ExecPrepared(
            fo_dbManager_PrepareStamement(
            dbManager,
            "updatereporttable",
            "INSERT INTO reportgen(upload_fk, job_fk, filepath) VALUES($1,$2,$3)",
            int, int, char*
            ),
            uploadId,
            fo_scheduler_jobId(),
            finaldocxpath
            )
            );

    if (outputPkgName) {
      free(outputPkgName);
      outputPkgName = NULL;
    }
    if (finaldocxpath) {
      free(finaldocxpath);
    }

    if (fullpath) {
      free(fullpath);
    }

    int uloop2;
    for (uloop2 = 0; uloop2 < 3; uloop2++) {
      if (tbcolSkewed[uloop2])
        free(tbcolSkewed[uloop2]);
    }

    table_free(tableOthers);
    table_free(tableCopyright);
    table_free(tableSecurity);
    table_free(tableMatches);
#ifdef REPORT_KEYWORDS
    table_free(tableKeywords);
#endif

    /*free timestampstring*/
    if (formattedtime) {
      free(formattedtime);
      formattedtime = NULL;
    }
    /*free targetstructure name*/
    if (fullpathWithoutSlash) {
      free(fullpathWithoutSlash);
      fullpathWithoutSlash = NULL;
    }

    mxmlDelete(xml);
    mxmlDelete(hxml);
    mxmlDelete(fxml);
    mxmlDelete(refxml);
    mxmlDelete(fontxml);
    mxmlDelete(stylexml);
    mxmlDelete(numberingxml);
    mxmlDelete(contentxml);
    mxmlDelete(appxml);
    mxmlDelete(corexml);
    mxmlDelete(hiddenrelsxml);
    fo_WriteARS(pgConn, ars_pk, uploadId, agent_pk, AGENT_ARS, NULL, 1);
  }

  fo_scheduler_disconnect(0);

  return 0;
}
