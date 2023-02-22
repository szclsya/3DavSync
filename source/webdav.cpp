#include "webdav.h"

#include <sys/stat.h>
#include <dirent.h>
#include <regex>
#include "curl/curl.h"
#include "curl/easy.h"
#include <tinyxml2/tinyxml2.h> 

using namespace std;
using namespace tinyxml2;

WebDavClient::WebDavClient(string w, string l) : web_root(w), local_root(l) {
    curl = curl_easy_init();
    reset();
}

WebDavClient::~WebDavClient() {
    curl_easy_cleanup(this->curl);
}

void WebDavClient::reset() {
    curl_easy_reset(this->curl);
    curl_easy_setopt(this->curl, CURLOPT_USERAGENT, "3DavSync 0.1.0");
    curl_easy_setopt(this->curl, CURLOPT_CONNECTTIMEOUT, 50L);
    curl_easy_setopt(this->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(this->curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(this->curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(this->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->curl, CURLOPT_PIPEWAIT, 1L);
    //curl_easy_setopt(this->curl, CURLOPT_VERBOSE, 1L);
    if (this->use_basic_auth) {
        curl_easy_setopt(this->curl, CURLOPT_USERNAME, this->username.c_str());
        curl_easy_setopt(this->curl, CURLOPT_PASSWORD, this->password.c_str());
        curl_easy_setopt(this->curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    }
}

void WebDavClient::set_basic_auth(std::string username, std::string password) {
    this->username = username;
    this->password = password;
    this->use_basic_auth = true;
    this->reset();
}

string formulate_actual_url(string &root, string &rel_path) {
    if (!rel_path.empty()) {
        string escaped_string = curl_easy_escape(NULL, rel_path.c_str(), 0);
        string escaped_string_with_slash = regex_replace(escaped_string, regex("%2F"), "/");
        string res = root + escaped_string_with_slash;
        return res;
    } else {
        return root;
    }
}

bool WebDavClient::mkcol(string web_rel_path, optional<u64> mtime) {
    string actual_url = formulate_actual_url(this->web_root, web_rel_path);
    // Test if directory existed already
    curl_easy_setopt(this->curl, CURLOPT_URL, actual_url.c_str());
    curl_easy_setopt(this->curl, CURLOPT_NOBODY, 1L);
    CURLcode head_res = curl_easy_perform(this->curl);
    if (head_res == CURLE_OK) {
        this->reset();
        return true;
    }

    this->reset();
    curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, "MKCOL");
    curl_easy_setopt(this->curl, CURLOPT_URL, actual_url.c_str());
    if (mtime) {
        u64 actual_mtime = mtime.value();
        struct curl_slist *list = NULL;
        string oc_mtime = string("X-OC-Mtime: " + to_string(actual_mtime)).c_str();
        list = curl_slist_append(list, oc_mtime.c_str());
        curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, list);
    }

    CURLcode curl_res = curl_easy_perform(this->curl);
    if (curl_res != CURLE_OK) {
        long response_code;
        curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &response_code);
        printf("curl MKCOL failed: %s (%d)\n", curl_easy_strerror(curl_res), curl_res);
        printf("Location: %s\n", actual_url.c_str());
        printf("Response code: %ld\n", response_code);
        this->reset();
        return false;
    } else {
        this->reset();
        return true;
    }
}

bool WebDavClient::pull(string path, string web_rel_path) {
    const char* c_path = path.c_str();
    if (this->curl) {
        // Formulate and fill actual url
        string actual_url = formulate_actual_url(this->web_root, web_rel_path);
        curl_easy_setopt(curl, CURLOPT_URL, actual_url.c_str());

        // Open a file at that path, overwrite if exists
        FILE *fp = fopen(c_path, "w");
        if (fp) {
            curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, fp);
            CURLcode res = curl_easy_perform(curl);
            fclose(fp);
            if (res != 0) {
              long response_code;
              curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &response_code);
              printf("error getting file %s: %s\n", c_path, curl_easy_strerror(res));
              printf("Location: %s\n", actual_url.c_str());
              printf("Response code: %ld\n", response_code);
              this->reset();
              return false;
            }
        } else {
            printf("can't open %s for writing: %s\n", c_path, strerror(errno));
        }
    } else {
        printf("can't initalize curl!\n");
    }
    this->reset();
    return true;
}


static int seek_helper(void *userp, curl_off_t offset, int origin) {
  FILE *fp = (FILE *) userp;
 
  if(-1 == fseek(fp, (long) offset, origin))
    /* couldn't seek */
    return CURL_SEEKFUNC_CANTSEEK;
 
  return CURL_SEEKFUNC_OK; /* success! */
}

bool WebDavClient::push(string path, string web_rel_path) {
    const char* c_path = path.c_str();
    if (this->curl) {
        // Formulate and fill actual url
        string actual_url = formulate_actual_url(this->web_root, web_rel_path);
        curl_easy_setopt(curl, CURLOPT_URL, actual_url.c_str());

        // Open a file at that path to read
        FILE* fp = fopen(c_path, "r");
        if (fp) {
            // Read file metadata
            struct stat file_info;
            fstat(fileno(fp), &file_info);
            // Set curl reading stuff
            curl_easy_setopt(this->curl, CURLOPT_READDATA, fp);
            curl_easy_setopt(this->curl, CURLOPT_SEEKFUNCTION, seek_helper);
            // Prepare the headers
            // For Nextcloud/ownCloud, we can ask the server to use our mtime
            u64 mtime;
            archive_getmtime(path.c_str(), &mtime);
            struct curl_slist *list = NULL;
            string t = string("X-OC-Mtime: " + to_string(mtime)).c_str();
            list = curl_slist_append(list, t.c_str());
            curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, list);
            // We are uploading!
            curl_easy_setopt(this->curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(this->curl, CURLOPT_INFILESIZE, (curl_off_t)file_info.st_size);
            CURLcode res = curl_easy_perform(curl);
            fclose(fp);
            if (res != CURLE_OK) {
                long response_code;
                curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &response_code);
                printf("error pushing file %s: %s\n", c_path, curl_easy_strerror(res));
                printf("Location: %s\n", actual_url.c_str());
                printf("Response code: %ld\n", response_code);
                this->reset();
                return false;
            }
        } else {
            printf("can't open %s for writing: %s\n", c_path, strerror(errno));
        }
    } else {
        printf("can't initalize curl!\n");
    }

    this->reset();
    return true;
}

/// Write the curl response to a std::string
size_t curl_write_to_string(void *ptr, size_t size, size_t nmemb,
                            string *s) {
    s->append(static_cast<char *>(ptr), size*nmemb);
    return size*nmemb;
}

optional<vector<FileEntry>> normalize_filelist(optional<vector<FileEntry>> i) {
    if (i) {
        auto files = i.value();
        if (files.empty()) {
            return nullopt;
        } else if (files.size() == 1) {
            // Only contains the root directory, we don't care about that
            files.erase(files.begin());
        } else if (files.size() > 1) {
            // Seems to be a query on collection
            // Get root first
            if (files[0].path.back() == '/') {
                string root = files[0].path.c_str();
                root.pop_back();
                for (FileEntry& file : files) {
                    file.path.erase(0, root.length());
                }
            }
        }
        return files;
    } else {
        return nullopt;
    }
}

const char* query = R"(<?xml version="1.0"?>
<d:propfind  xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns" xmlns:nc="http://nextcloud.org/ns">
 <d:prop>
  <d:getlastmodified />
  <d:getcontentlength />
  <d:resourcetype />
  <oc:checksums />
 </d:prop>
</d:propfind>)";

/// Read the timestamp from WebDAV server
optional<vector<FileEntry>> WebDavClient::get_remote_files() {
    // Use PROPFIND to fetch file metadata
    if (this->curl) {
        string response = "";
        // Formulate and fill url
        curl_easy_setopt(curl, CURLOPT_URL, this->web_root.c_str());
        // Read XML content
        curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, curl_write_to_string);
        curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(this->curl, CURLOPT_POSTFIELDS, query);
        struct curl_slist *list = NULL;
        list = curl_slist_append(list, "Depth: infinity");
        curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, list);

        CURLcode curl_res = curl_easy_perform(this->curl);
        if (curl_res != CURLE_OK) {
            long response_code;
            curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &response_code);
            printf("curl PROPFIND failed: %s (%d)\n", curl_easy_strerror(curl_res), curl_res);
            printf("Response code: %ld\n", response_code);
            this->reset();
            return nullopt;
        }
        this->reset();
        
        // Now read it!
        XMLDocument doc;
        XMLError parse_res = doc.Parse(response.c_str());

        vector<FileEntry> res;
        if (parse_res == tinyxml2::XML_SUCCESS) {
            XMLElement* root = doc.RootElement();
            for (XMLElement* e = root->FirstChildElement(); e != NULL; e = e->NextSiblingElement()) {
                string path;
                time_t time;
                bool folder = false;
                int size;
                // Get file path
                if (e->FirstChildElement("d:href")) {
                    const char* text = e->FirstChildElement("d:href")->GetText();
                    // The path here is escaped. Convert them back to unescaped form
                    char* decoded = curl_easy_unescape(this->curl, text, 0, NULL);
                    path = string(decoded);
                } else {
                    printf("malformed WebDAV response: missing d:href in PROPFIND\n");
                    return nullopt;
                }
                if (e->FirstChildElement("d:propstat")) {
                    if (e->FirstChildElement("d:propstat")->FirstChildElement("d:prop")) {
                        XMLElement* prop = e->FirstChildElement("d:propstat")->FirstChildElement("d:prop");
                        if (prop->FirstChildElement("d:getlastmodified")) {
                            string time_str = prop->FirstChildElement("d:getlastmodified")->GetText();
                            time = curl_getdate(time_str.c_str(), NULL);
                        } else {
                            printf("malformed WebDAV response: missing d:getlastmodified in PROPFIND\n");
                            return nullopt;
                        }
                        if (prop->FirstChildElement("d:getcontentlength")) {
                            string size_str = prop->FirstChildElement("d:getcontentlength")->GetText();
                            size = stoi(size_str);
                        } else {
                            size = 0;
                        }
                        if (prop->FirstChildElement("d:resourcetype")
                            && prop->FirstChildElement("d:resourcetype")->FirstChildElement("d:collection")) {
                            folder = true;
                        }
                    } else {
                        printf("malformed WebDAV response: missing d:prop in PROPFIND\n");
                        return nullopt;
                    }
                } else {
                    printf("malformed WebDAV response: missing d:propstat in PROPFIND\n");
                    return nullopt;
                }

                res.push_back(FileEntry { path, time, folder, size });
            }
        } else {
            printf("malformed XML response: %d\n", parse_res);
            return nullopt;
        }
        // Turn file paths into canonical form
        auto out = normalize_filelist(res);
        return out;
    } else {
        printf("can't initalize curl!\n");
        return nullopt;
    }
}

optional<FileEntry> get_file(vector<FileEntry>* files, string path) {
    for (unsigned i = 0; i < files->size(); i++) {
        if ((*files)[i].path == path) {
            FileEntry f = (*files)[i];
            files->erase(files->begin() + i);
            return f;
        }
    }
    return nullopt;
}

vector<pair<string, bool>> recursively_get_dir(string base_path, string ext_path="") {
    vector<pair<string, bool>> paths;

    DIR *dir;
    struct dirent *ent;
    string path = base_path + ext_path;
    if ((dir = opendir(path.c_str())) != NULL) {
        paths.push_back(pair(ext_path + "/", true));
        while ((ent = readdir(dir)) != NULL) {
            vector<pair<string, bool>> subdir = recursively_get_dir(base_path, ext_path + "/" + ent->d_name);
            paths.insert(paths.end(), subdir.begin(), subdir.end());
        }
    } else {
        if (ext_path != "") {
            paths.push_back(make_pair(ext_path, false));
        } else {
            printf("directory %s not found\n", path.c_str());
        }
    }
    closedir(dir);
    return paths;
}

bool WebDavClient::compareAndUpdate() {
    bool success = true;

    struct stat rootstat;
    if (!stat(this->local_root.c_str(), &rootstat)) {
        // Create directory
        mkdir(this->local_root.c_str(), 0777);
    } else if (!S_ISDIR(rootstat.st_mode)) {
        printf(CONSOLE_RED "specified local dir is not a dir!" CONSOLE_RESET);
        return false;
    }

    this->mkcol("", nullopt);

    optional<vector<FileEntry>> remote_files_optional = this->get_remote_files();
    if (!remote_files_optional) {
        printf("failed to fetch remote file list\n");
        return false;
    }

    vector<FileEntry> remote_files = remote_files_optional.value();
    vector<pair<string, bool>> local_files = recursively_get_dir(this->local_root);
    for (auto [ path, is_dir ] : local_files) {
        string local_real_path = this->local_root + path;
        u64 local_mtime;
        archive_getmtime(local_real_path.c_str(), &local_mtime);

        optional<FileEntry> remote_file_optional = get_file(&remote_files, path);
        if (remote_file_optional) {
            FileEntry remote_file = remote_file_optional.value();

            if (is_dir) {
                // Local && Remote directory exists, we are fine
            } else if (local_mtime > remote_file.last_modified) {
                // Upload local version
                if (!this->push(local_real_path, path)) {
                    printf(CONSOLE_RED "%s: local modified, upload failed.\n", path.c_str());
                    printf(CONSOLE_RESET);
                    success = false;
                } else {
                    printf("%s: local modified, uploaded.\n", path.c_str());
                }
            } else if (local_mtime < remote_file.last_modified) {
                // Pull remote version
                if (!this->pull(local_real_path, remote_file.path)) {
                    printf(CONSOLE_RED "%s: remote modified, download failed.\n", path.c_str());
                    printf(CONSOLE_RESET);
                    success = false;
                } else {
                    printf("%s: remote modified, downloaded.\n", remote_file.path.c_str());
                }
            } else {
                // Identical, don't do anything
            }
        } else {
            // Remote file DNE, upload local version
            if (is_dir) {
                this->mkcol(path, local_mtime);
            } else {
                if (!this->push(local_real_path, path)) {
                    printf(CONSOLE_RED "%s: new, upload failed.\n\n", path.c_str());
                    printf(CONSOLE_RESET);
                    success = false;
                } else {
                    printf("%s: new, uploaded.\n\n", path.c_str());
                }
            }
        }
    }
    // Pull the remaining remote files
    for (FileEntry remote_file : remote_files) {
        string real_local_path = this->local_root + remote_file.path;
        if (remote_file.path == "/") {
            continue;
        } else if (remote_file.folder) {
            int status = mkdir(real_local_path.c_str(), 0777);
            if (status != 0) {
                printf("can't create local dir %s: %s\n", real_local_path.c_str(), strerror(errno));
                success = false;
            }
        } else {
            // It's a file
            if (!this->pull(real_local_path, remote_file.path)) {
                printf("can't pull remote file %s\n", remote_file.path.c_str());
                success = false;
            } else {
                printf("pulled remote file %s\n", remote_file.path.c_str());
            }
        }
    }
    return success;
}
