#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <regex.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>

typedef struct{
    char *response;
    size_t size;
}memory;

static size_t cb(const char *data, size_t size, size_t nmemb, void *clientp){
	size_t realsize = size * nmemb;
	memory *mem = (memory *)clientp;
	
	char *ptr = realloc(mem->response, mem->size + realsize + 1);
	if(!ptr)
		return 0;  /* out of memory */
	
	mem->response = ptr;
	memcpy(&(mem->response[mem->size]), data, realsize);
	mem->size += realsize;
	mem->response[mem->size] = 0;
	
	return realsize;
}

int main(int argc, char **argv){
	CURL *curl = curl_easy_init();
    memory response = {0};

	struct curl_slist *headers = NULL;
	curl_slist_append(headers, "Cookie: session=53616c7465645f5f2eec47c02bf818203d045ffeaad6fcb364a06ddaadf9046acb1b3441fb46066b8a7b7420576aea826df21d94ff2c824bde5bddacb06d2c34");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_URL, "https://adventofcode.com/2024/day/1");
	curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

	CURLcode code = curl_easy_perform(curl);

    if(code != CURLE_OK)
        printf("[%i] Response: %s\n", code, response.response != NULL ? response.response : "");
    else
        printf("%s\n", response.response);

	htmlDocPtr html = htmlParseDoc(response.response, "UTF8");
	if(html == NULL){
		printf("HTML parse failed!\n");
		exit(-1);
	}

	xmlNodePtr node = xmlDocGetRootElement(html);
	if(html == NULL){
		printf("HTML was empty!\n");
		exit(-1);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return 0;
}
