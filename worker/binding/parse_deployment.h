#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <rapidjson/document.h>

using namespace std;

typedef struct deployment_config_s {
    string metadata_bucket;
    string source_bucket;
    string source_endpoint;
    map<string, map<string, vector<string> > > component_configs;
} deployment_config;

deployment_config* ParseDeployment(const char* app_name);
