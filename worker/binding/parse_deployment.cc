#include "parse_deployment.h"

deployment_config* ParseDeployment(const char* app_name) {
  deployment_config* config = new deployment_config();

  string file_name("./apps/");
  file_name.append(app_name);

  ifstream ifs(file_name.c_str());
  string content((istreambuf_iterator<char>(ifs)),
                  (istreambuf_iterator<char>()));

  rapidjson::Document doc;
  if (doc.Parse(content.c_str()).HasParseError()) {
    cerr << "Unable to parse deployment config for app: "
         << app_name << endl;
    exit(1);
  }

  assert(doc.IsObject());

  {
      rapidjson::Value& buckets = doc["depcfg"]["buckets"];
      rapidjson::Value& queues = doc["depcfg"]["queue"];
      rapidjson::Value& workspace = doc["depcfg"]["workspace"];
      rapidjson::Value& source = doc["depcfg"]["source"];

      assert(buckets.IsArray());
      assert(queues.IsArray());
      assert(workspace.IsObject());
      assert(source.IsObject());

      map<string, vector<string> > buckets_info;
      for(rapidjson::SizeType i = 0; i < buckets.Size(); i++) {
          vector<string> bucket_info;

          rapidjson::Value& bucket_name = buckets[i]["bucket_name"];
          rapidjson::Value& alias = buckets[i]["alias"];

          bucket_info.push_back(bucket_name.GetString());
          bucket_info.push_back(alias.GetString());

          buckets_info[alias.GetString()] = bucket_info;
      }
      config->component_configs["buckets"] = buckets_info;

      map<string, vector<string> > queues_info;
      for(rapidjson::SizeType i = 0; i < queues.Size(); i++) {
          vector<string> queue_info;

          rapidjson::Value& provider = queues[i]["provider"];
          rapidjson::Value& queue_name = queues[i]["queue_name"];
          rapidjson::Value& endpoint = queues[i]["endpoint"];
          rapidjson::Value& alias = queues[i]["alias"];

          queue_info.push_back(provider.GetString());
          queue_info.push_back(endpoint.GetString());
          queue_info.push_back(alias.GetString());
          queue_info.push_back(queue_name.GetString());

          queues_info[provider.GetString()] = queue_info;
      }
      config->component_configs["queue"] = queues_info;

      config->metadata_bucket.assign(workspace["metadata_bucket"].GetString());
      config->source_bucket.assign(source["source_bucket"].GetString());
      config->source_endpoint.assign("localhost");
  }
  return config;
}
