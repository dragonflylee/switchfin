#include "client/webdav.hpp"
#include <curl/curl.h>
#include <tinyxml2/tinyxml2.h>
#include <sstream>

namespace remote {

Webdav::Webdav(const std::string& url, const AppRemote& conf) {
    HTTP::Header h{"Accept-Charset: utf-8", "Depth: 1"};
    HTTP::set_option(this->c, HTTP::Timeout{}, h);
    this->init(conf, this->c);
    this->host = url.substr(0, url.find("/", url.find("://") + 3));
    brls::Logger::debug("remote::Webdav host {}", url);
}

static std::string getNamespacePrefix(tinyxml2::XMLElement* root, const std::string& nsURI) {
    for (const tinyxml2::XMLAttribute* attr = root->FirstAttribute(); attr; attr = attr->Next()) {
        std::string name = attr->Name();
        if (nsURI.compare(attr->Value()) == 0) {
            auto pos = name.find(':');
            if (pos != std::string::npos) {
                return name.substr(pos + 1);
            } else {
                return "";  // Default namespace (no prefix)
            }
        }
    }
    return "";  // No namespace found
}

std::vector<DirEntry> Webdav::list(const std::string& path) {
    std::stringstream ss;
    std::string url = path.rfind("webdav") == 0 ? "http" + path.substr(6) : path;
    int status = this->c.propfind(url, &ss);
    if (status != 207) {
        throw remote_error(fmt::format("webdav propfind {}", status));
    }

    tinyxml2::XMLDocument doc = tinyxml2::XMLDocument();
    tinyxml2::XMLError error = doc.Parse(ss.str().c_str());
    if (error != tinyxml2::XMLError::XML_SUCCESS) {
        const char* name = tinyxml2::XMLDocument::ErrorIDToName(error);
        throw remote_error(name);
    }

    tinyxml2::XMLElement* root = doc.RootElement();
    std::string nsPrefix = getNamespacePrefix(root, "DAV:");
    if (!nsPrefix.empty()) nsPrefix.append(":");  // Append colon if non-empty

    tinyxml2::XMLElement* respElem = root->FirstChildElement((nsPrefix + "response").c_str());

    std::vector<DirEntry> s;

    while (respElem) {
        DirEntry item;
        item.fileSize = 0;

        tinyxml2::XMLElement* hrefElem = respElem->FirstChildElement((nsPrefix + "href").c_str());
        if (hrefElem) {
            std::string hrefText = hrefElem->GetText();
            // href can be absolute URI or relative reference. ALWAYS convert to relative reference
            if (hrefText.find(this->host) == 0) {
                item.path = hrefText;
            } else {
                item.path = this->host + hrefText;
            }
        }

        tinyxml2::XMLElement* propstatElem = respElem->FirstChildElement((nsPrefix + "propstat").c_str());
        if (propstatElem) {
            tinyxml2::XMLElement* propElem = propstatElem->FirstChildElement((nsPrefix + "prop").c_str());
            if (propElem) {
                tinyxml2::XMLElement* displaynameElem = propElem->FirstChildElement((nsPrefix + "displayname").c_str());
                if (displaynameElem) {
                    const char* displayname = displaynameElem->GetText();
                    if (displayname) item.name = displayname;
                }

                tinyxml2::XMLElement* resElem = propElem->FirstChildElement((nsPrefix + "resourcetype").c_str());
                if (resElem)
                    item.type = resElem->FirstChildElement((nsPrefix + "collection").c_str()) != nullptr
                                    ? EntryType::DIR
                                    : EntryType::FILE;

                tinyxml2::XMLElement* lenElem = propElem->FirstChildElement((nsPrefix + "getcontentlength").c_str());
                if (lenElem) {
                    const char* sizeStr = lenElem->GetText();
                    if (sizeStr) item.fileSize = std::stoull(sizeStr);
                }
                tinyxml2::XMLElement* timeElem = propElem->FirstChildElement((nsPrefix + "getlastmodified").c_str());
                if (timeElem) {
                    const char* timeStr = timeElem->GetText();
                    if (timeStr) {
                        std::stringstream ss(timeStr);
                        ss >> std::get_time(&item.modified, "%a, %d %b %Y %H:%M:%S %Z");
                    }
                }
            }

            if (item.name.empty()) {
                std::string name = item.path;
                if (name.back() == '/') name.pop_back();

                auto pos = name.find_last_of("/");
                if (pos > 0) name = name.substr(pos + 1);
                char* unescape = curl_unescape(name.c_str(), name.size());
                item.name = unescape;
                curl_free(unescape);
            }
        }

        respElem = respElem->NextSiblingElement((nsPrefix + "response").c_str());

        s.push_back(item);
    }

    if (s.size() > 0) {
        s[0].type = EntryType::UP;
    }

    return s;
}

}  // namespace remote