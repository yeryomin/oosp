#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <curl/curl.h>
#include <getopt.h>
#include <sys/stat.h>

#define OOSPSRV_FILE	"/tmp/oosp-servers.xml"
#define OOSPSRV_URL	"http://www.speedtest.net/speedtest-servers-static.php"
#define USERAGENT	"hackster/1.0"
#define DLFILE		"random4000x4000.jpg"
#define ULSIZE		500000
#define PROGRESS_INT	0.5
#define DIR_DOWNLOAD	0
#define DIR_UPLOAD	1

#ifndef PACKAGE_NAME
#define PACKAGE_NAME		"oosp"
#endif
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION		"(undefined)"
#endif
#ifndef PACKAGE_BUGREPORT
#define PACKAGE_BUGREPORT	"roman@advem.lv"
#endif

typedef struct mem_t {
	char *chunk;
	size_t size;
} mem_t;

typedef struct progress_t {
	int direction;
	double lastruntime;
	size_t last;
	int done;
	CURL *curl;
} progress_t;

typedef struct srv_t {
	char *source;
	char *id;
	char *country;
	char *city;
	char *provider;
} srv_t;

static const char *opt_string = ":c:C:i:p:s:u:h";
static struct option long_options[] = {
	{ "country", required_argument, NULL, 'C' },
	{ "city", required_argument, NULL, 'c' },
	{ "provider", required_argument, NULL, 'p' },
	{ "source", required_argument, NULL, 's' },
	{ "id", required_argument, NULL, 'i' },
	{ "ul-size", required_argument, NULL, 'u' },
	{ "help", no_argument, NULL, 'h' },
	{ 0, 0, 0, 0 },
};

static void usage( void )
{
	printf(
	"    %s, v%s\n"
	"    Usage: %s [OPTIONS]\n"
	"\n"
	"	Options:\n"
	"	-C, --country		choose a server from this country\n"
	"	-c, --city		choose a server from this city\n"
	"	-p, --provider		choose a server from this provider\n"
	"	-s, --source		server config source (file or url)\n"
	"	-i, --id		choose a server with this id\n"
	"	-u, --ul-size		upload size\n"
	"	-h, --help		show this help message\n"
	"\n"
	"    Report bugs to %s\n"
	"\n",
	PACKAGE_NAME, PACKAGE_VERSION, PACKAGE_NAME, PACKAGE_BUGREPORT );
}

xmlNode *find_node( xmlNode *parent, const char *name )
{
	xmlNode *iter = NULL;

	if ( !parent || !name )
		return NULL;

	for ( iter = parent->children; iter; iter = iter->next ) {
		if ( iter->type != XML_ELEMENT_NODE )
			continue;
		if ( !strncmp( iter->name, name, strlen(name) + 1 ) )
			return iter;
	}
	return NULL;
}

xmlNode *find_next_node( xmlNode *prev, const char *name )
{
	xmlNode *iter = NULL;

	if ( !prev || !name )
		return NULL;

	for ( iter = prev->next; iter; iter = iter->next ) {
		if ( iter->type != XML_ELEMENT_NODE )
			continue;
		if ( !strncmp( iter->name, name, strlen(name) + 1 ) )
			return iter;
	}
	return NULL;
}

xmlChar *get_node_attribute( xmlNode *node, const char *name )
{
	xmlAttr *attr  = NULL;

	if ( !node || !name )
		return NULL;

	attr = node->properties;
	while( attr && attr->name && attr->children ) {
		if ( strncmp( attr->name, name, strlen(name) + 1 ) ) {
			attr = attr->next;
			continue;
		}
		return xmlNodeListGetString( node->doc, attr->children, 1 );
	}

	return NULL;
}

xmlChar *get_node_content( xmlNode *node )
{
	if ( !node || !node->children )
		return NULL;

	if ( node->children->content )
		return strdup( node->children->content );

	return NULL;
}

static size_t write_mem( void *curl_buf, size_t size,
			 size_t nmemb, void *userp )
{
	size_t curl_size = size * nmemb;
	mem_t *mem = (mem_t *)userp;

	if ( !mem->chunk ) {
		mem->size += curl_size;
		return curl_size;
	}

	mem->chunk = (char *)realloc( mem->chunk, mem->size + curl_size + 1 );
	if ( !mem->chunk )
		return 0;

	memcpy( &(mem->chunk[ mem->size ]), curl_buf, curl_size );
	mem->size += curl_size;
	mem->chunk[ mem->size ] = 0;

	return curl_size;
}

static size_t read_mem( void *curl_buf, size_t size,
			size_t nmemb, void *userp )
{
	size_t curl_size = nmemb * size;
	mem_t *mem = (mem_t *)userp;
	size_t to_copy = (mem->size < curl_size) ?
				mem->size :
				curl_size;

	memcpy( curl_buf, mem->chunk, to_copy );
	mem->size -= to_copy;
	mem->chunk += to_copy;

	return to_copy;
}

static int xferinfo( void *p,
		     curl_off_t dltotal, curl_off_t dlnow,
		     curl_off_t ultotal, curl_off_t ulnow )
{
	progress_t *myp = (progress_t *)p;
	double curtime = 0;
	double timediff = 0;
	size_t xferdiff = 0;
	double cmbps = 0;
	double ambps = 0;
	curl_off_t total = dltotal;
	curl_off_t now = dlnow;

	if ( myp->direction == DIR_UPLOAD ) {
		total = ultotal;
		now = ulnow;
	}

	if ( total < 1 )
		return 0;

	if ( myp->done )
		return 0;

	curl_easy_getinfo( myp->curl, CURLINFO_TOTAL_TIME, &curtime );
	timediff = curtime - myp->lastruntime;

	if ( (timediff > PROGRESS_INT && now > 0) || now == total ) {
		myp->lastruntime = curtime;
		if ( myp->direction == DIR_UPLOAD )
			printf("UL");
		else
			printf("DL");
		printf( ": %lu of %lu, done: %.0f%%",
					now, total,
					(double)(now/(total/100)) );

		xferdiff = now - myp->last;
		myp->last = now;

		if ( xferdiff ) {
			cmbps = (double)(((xferdiff/timediff)*8)/1000000);
			ambps = (double)(((now/curtime)*8)/1000000);
			printf( ", speed: %.2f Mbps, average: %.2f Mbps",
				cmbps, ambps );
		}

		printf( "\r" );
		fflush(stdout);
		if ( now == total )
			myp->done = 1;
	}

	if ( now == total )
		printf("\n");

	return 0;
}

int download( const char *url )
{
	if ( !url )
		return -1;

	CURL *curl = curl_easy_init();
	CURLcode res = CURLE_OK;
	if ( !curl )
		return -1;

	mem_t mem;
	mem.chunk = NULL; /* malloc to write to memory
			     (bad idea for embedded systems) */
	mem.size = 0;

	progress_t prog;
	prog.direction = DIR_DOWNLOAD;
	prog.lastruntime = 0;
	prog.last = 0;
	prog.done = 0;
	prog.curl = curl;

	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_mem );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *)&mem );
	curl_easy_setopt( curl, CURLOPT_USERAGENT, USERAGENT );
	curl_easy_setopt( curl, CURLOPT_XFERINFOFUNCTION, xferinfo );
	curl_easy_setopt( curl, CURLOPT_XFERINFODATA, &prog );
	curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 0L );

	res = curl_easy_perform( curl );
	if ( res != CURLE_OK )
		fprintf( stdout, "%s\n", curl_easy_strerror(res) );

	curl_easy_cleanup( curl );
	free( mem.chunk );
	curl_global_cleanup();

	return 0;
}

int upload( const char *url, mem_t *mem )
{
	if ( !url || !mem )
		return -1;

	CURL *curl = curl_easy_init();
	CURLcode res = CURLE_OK;
	if ( !curl )
		return -1;

	mem_t devnull;
	devnull.chunk = NULL;
	devnull.size = 0;

	progress_t prog;
	prog.direction = DIR_UPLOAD;
	prog.lastruntime = 0;
	prog.last = 0;
	prog.done = 0;
	prog.curl = curl;

	curl_easy_setopt( curl, CURLOPT_URL, url );
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, write_mem );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *)&devnull );
	curl_easy_setopt( curl, CURLOPT_USERAGENT, USERAGENT );
	curl_easy_setopt( curl, CURLOPT_XFERINFOFUNCTION, xferinfo );
	curl_easy_setopt( curl, CURLOPT_XFERINFODATA, &prog );
	curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 0L );

	curl_easy_setopt( curl, CURLOPT_UPLOAD, 1L );
	curl_easy_setopt( curl, CURLOPT_PUT, 1L );
	curl_easy_setopt( curl, CURLOPT_URL, url );

	curl_easy_setopt( curl, CURLOPT_READFUNCTION, read_mem );
	curl_easy_setopt( curl, CURLOPT_READDATA, (void *)mem );
	curl_easy_setopt( curl, CURLOPT_INFILESIZE_LARGE,
				(curl_off_t)mem->size );

	res = curl_easy_perform( curl );
	if ( res != CURLE_OK )
		fprintf( stdout, "%s\n", curl_easy_strerror(res) );

	curl_easy_cleanup( curl );
	curl_global_cleanup();

	return 0;
}

int get_speedtest_servers( const char *file )
{
	if ( !file )
		return -1;

	FILE *fp = fopen( file, "w");
	if ( !fp )
		return -1;

	CURL *curl = curl_easy_init();
	CURLcode res = CURLE_OK;
	if ( !curl ) {
		fclose( fp );
		return -1;
	}

	curl_easy_setopt( curl, CURLOPT_URL, OOSPSRV_URL );
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, fp );
	curl_easy_setopt( curl, CURLOPT_USERAGENT, USERAGENT );
	curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1 );

	res = curl_easy_perform( curl );
	if ( res != CURLE_OK )
		fprintf( stdout, "%s\n", curl_easy_strerror(res) );

	curl_easy_cleanup( curl );
	curl_global_cleanup();

	fclose( fp );

	return 0;
}

char *mkdlurl( const char *upurl )
{
	char *dlurl = NULL;
	char *end = NULL;
	char *file = DLFILE;
	size_t len = 0;

	end = strrchr( upurl, '/' );
	len = end - (char *)upurl;
	dlurl = (char *)malloc( strlen(file) + len + 3 );
	if ( !dlurl )
		return NULL;

	snprintf( dlurl, len + 1, "%s", upurl );
	snprintf( dlurl + len, strlen(file) + 2, "/%s", file );

	return dlurl;
}

int main( int argc, char **argv )
{
	xmlDoc *servers = NULL;
	xmlNode *root = NULL;
	xmlChar *str = NULL;
	xmlChar *country = NULL;
	xmlChar *city = NULL;
	xmlChar *sponsor = NULL;
	xmlNode *cur_node = NULL;
	xmlNode *srv_node = NULL;
	char *start = NULL;
	char *upurl = NULL;
	char *dlurl = NULL;
	size_t ulsize = ULSIZE;

	mem_t mem;
	srv_t srv;

	srv.id = NULL;
	srv.country = NULL;
	srv.city = NULL;
	srv.provider = NULL;
	srv.source = NULL;

	int opt;
	int option_index = 0;
	size_t optlen = 0;

	while ( (opt = getopt_long( argc, argv,
				    opt_string, long_options,
				    &option_index )) != -1 )
	{
		switch( opt ) {
		case 'i':
			optlen = strlen(optarg) + 1;
			srv.id = (char *)malloc( optlen );
			if ( !srv.id )
				goto exit;
			snprintf( srv.id, optlen, "%s", optarg );
			break;
		case 'c':
			optlen = strlen(optarg) + 1;
			srv.city = (char *)malloc( optlen );
			if ( !srv.city )
				goto exit;
			snprintf( srv.city, optlen, "%s", optarg );
			break;
		case 'C':
			optlen = strlen(optarg) + 1;
			srv.country = (char *)malloc( optlen );
			if ( !srv.country )
				goto exit;
			snprintf( srv.country, optlen, "%s", optarg );
			break;
		case 'p':
			optlen = strlen(optarg) + 1;
			srv.provider = (char *)malloc( optlen );
			if ( !srv.provider )
				goto exit;
			snprintf( srv.provider, optlen, "%s", optarg );
			break;
		case 's':
			optlen = strlen(optarg) + 1;
			srv.source = (char *)malloc( optlen );
			if ( !srv.source )
				goto exit;
			snprintf( srv.source, optlen, "%s", optarg );
			break;
		case 'u':
			ulsize = atol( optarg );
			break;
		case 'h':
			usage();
			goto exit;;
		default:
			printf( "%s: option -%c is invalid\n", argv[0], optopt );
			goto exit;
		}
	}

	/* initialize xml library */
	LIBXML_TEST_VERSION

	if ( !srv.source ) {
		printf( "server list source not given, using default\n" );
		srv.source = (char *)malloc( strlen(OOSPSRV_FILE) + 1 );
		snprintf( srv.source, strlen(OOSPSRV_FILE) + 1, "%s",
							OOSPSRV_FILE );
		/* TODO: use already downloaded file if any */
		get_speedtest_servers( srv.source );
	}

	servers = xmlReadFile( srv.source, NULL, 0 );
	if ( !servers ) {
		printf( "could not parse server list %s\n", srv.source );
		goto exit;
	}

	/* root element node */
	root = xmlDocGetRootElement( servers );

	cur_node = find_node( root, "servers" );
	for ( cur_node = find_node( cur_node, "server" );
	      cur_node;
	      cur_node = find_next_node( cur_node, "server" ) )
	{
		country = get_node_attribute( cur_node, "country" );
		city = get_node_attribute( cur_node, "name" );
		sponsor = get_node_attribute( cur_node, "sponsor" );

		if ( city && sponsor && country ) {
			if ( srv.city || srv.provider || srv.country )
				srv_node = cur_node;
			else
				printf( "%s, %s (%s)\n", country,
							 city,
							 sponsor );
			if ( srv.country && strncmp( country, srv.country,
						strlen(srv.country) + 1 ) )
				srv_node = NULL;
			if ( srv.city && strncmp( city, srv.city,
						strlen(srv.city) + 1 ) )
				srv_node = NULL;
			if ( srv.provider && strncmp( sponsor, srv.provider,
			     			strlen(srv.provider) + 1 ) )
				srv_node = NULL;
			if ( (srv.city || srv.provider || srv.country) &&
								srv_node )
			{
				printf( "Found server: %s, %s (%s)\n",
						country, city, sponsor );
				break;
			}
		}

		xmlFree( country );
		xmlFree( city );
		xmlFree( sponsor );
		country = NULL;
		city = NULL;
		sponsor = NULL;
	}

	/* TODO: bad idea for embedded! */
	mem.size = ulsize;
	mem.chunk = (char *)malloc( mem.size );
	if ( !mem.chunk )
		goto exit;

	start = mem.chunk;
	memset( mem.chunk, 'a', mem.size ); /* TODO: fill with random? */

	upurl = get_node_attribute( srv_node, "url" );
	if ( upurl ) {
		dlurl = mkdlurl( upurl );
		download( dlurl );
		upload( upurl, &mem );
	}

exit:
	free( start );
	free( upurl );
	free( dlurl );

	free( srv.id );
	free( srv.country );
	free( srv.city );
	free( srv.provider );
	free( srv.source );

	/* xml cleanups */
	xmlFreeDoc( servers );
	xmlFree( str );
	xmlFree( country );
	xmlFree( city );
	xmlFree( sponsor );
	xmlCleanupParser();

	return 0;
}
