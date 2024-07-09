#pragma once

#include <string>
#include <optional>
#include <ctime>
#include <vector>
#include <3ds.h>

#include <curl/curl.h>

struct FileEntry {
    std::string path;
    time_t last_modified;
    bool folder;
    int size;
};

class WebDavClient {
public:
    WebDavClient(std::string web_root, std::string local_root);
    ~WebDavClient();
    /// Configure this instance to use HTTP simple auth
    void set_basic_auth(std::string username, std::string password);
    /// Use Nextcloud's WebDAV API, or use the RFC API
    void set_nextcloud(bool nextcloud);
    /// Make a directory on the remote server
    bool mkcol(std::string web_path_rel, std::optional<u64> mtime);
    /// Push a file to the remote WebDAV collection
    bool push(std::string path, std::string web_path_rel);
    /// Pull a file from remote WebDAV collection
    bool pull(std::string path, std::string web_path_rel);
    /// Get a list of remote files
    std::optional<std::vector<FileEntry>> get_remote_files();
    /// compare the local file with the remote file specified by web_path_rel
    /// if local is newer, upload. if remote newer, pull and overwrite
    /// the remote path will be appended to web_root
    bool compareAndUpdate();
private:
    CURL *curl;
    std::string web_root; // Base URL
    std::string local_root;
    bool use_basic_auth;
    std::string username;
    std::string password;
    bool nextcloud;
    void reset();
};
