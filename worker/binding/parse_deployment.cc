#include "parse_deployment.h"

deployment_config* ParseDeployment() {
  deployment_config* config = new deployment_config();

  ifstream ifs("./deployment.json");
  string content((istreambuf_iterator<char>(ifs)),
                  (istreambuf_iterator<char>()));

  rapidjson::Document doc;
  if (doc.Parse(content.c_str()).HasParseError()) {
    std::cerr << "Unable to parse deployment.json, exiting!" << std::endl;
    exit(1);
  }

  assert(doc.IsObject());

  {
      rapidjson::Value& buckets = doc["buckets"];
      rapidjson::Value& queues = doc["queue"];
      rapidjson::Value& eventing = doc["workspace"];

      assert(buckets.IsArray());
      assert(queues.IsArray());
      assert(eventing.IsObject());

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

      config->metadata_bucket.assign(eventing["metadata_bucket"].GetString());
  }
  return config;
}
