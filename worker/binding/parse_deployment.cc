#include "parse_deployment.h"

map<string, map<string, vector<string> > > ParseDeployment() {
  map<string, map<string, vector<string> > > out;

  //ifstream ifs("./deployment.json");
  ifstream ifs("/Users/asingh/repo/go/src/github.com/abhi-bit/eventing/go_eventing/deployment.json");
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
      rapidjson::Value& n1ql = doc["n1ql"];
      assert(buckets.IsArray());
      assert(queues.IsArray());
      assert(n1ql.IsArray());

      map<string, vector<string> > buckets_info;
      for(rapidjson::SizeType i = 0; i < buckets.Size(); i++) {
          vector<string> bucket_info;

          rapidjson::Value& bucket_name = buckets[i]["bucket_name"];
          rapidjson::Value& endpoint = buckets[i]["endpoint"];
          rapidjson::Value& alias = buckets[i]["alias"];

          bucket_info.push_back(bucket_name.GetString());
          bucket_info.push_back(endpoint.GetString());
          bucket_info.push_back(alias.GetString());

          buckets_info[alias.GetString()] = bucket_info;
      }
      out["buckets"] = buckets_info;

      map<string, vector<string> > queues_info;
      for(rapidjson::SizeType i = 0; i < queues.Size(); i++) {
          vector<string> queue_info;

          rapidjson::Value& queue_name = queues[i]["queue_name"];
          rapidjson::Value& endpoint = queues[i]["endpoint"];
          rapidjson::Value& alias = queues[i]["alias"];

          queue_info.push_back(queue_name.GetString());
          queue_info.push_back(endpoint.GetString());
          queue_info.push_back(alias.GetString());

          queues_info[queue_name.GetString()] = queue_info;
      }
      out["queue"] = queues_info;


      map<string, vector<string> > n1qls_info;
      for(rapidjson::SizeType i = 0; i < n1ql.Size(); i++) {
          vector<string> n1ql_info;

          rapidjson::Value& bucket_name = n1ql[i]["bucket_name"];
          rapidjson::Value& endpoint = n1ql[i]["endpoint"];
          rapidjson::Value& n1ql_alias = n1ql[i]["alias"];

          n1ql_info.push_back(bucket_name.GetString());
          n1ql_info.push_back(endpoint.GetString());
          n1ql_info.push_back(n1ql_alias.GetString());

          n1qls_info[n1ql_alias.GetString()] = n1ql_info;
      }
      out["n1ql"] = n1qls_info;
  }
  return out;
}
