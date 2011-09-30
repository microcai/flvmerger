
#include <stdio.h>
#include <stdlib.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <netinet/in.h>

#define METADATACREATOR "microcai hired by sina"

#pragma pack(push)

#pragma pack(1)

struct flvbodytag {
	uint32_t	prv_tag_size;
	uint8_t	tag_type;
	uint8_t	tag_data_size[3];
	uint32_t	timestamp;
	uint8_t	stream_id[3];
};

#pragma pack(pop)

static FILE * out;

//我需要的信息有：
uint64_t duration;
uint64_t *durations;

int nb_files;
AVIndexEntry ** keyframes;
double			* nb_keyframes;
double			 nb_keyframes_all;

int		hasVideo,hasAudio,stereo;
int		hasKeyframe_positions;
double *datasizes;

double width, height;
double audiosamplerate,audiosamplesize;
double audiocodecid,videocodecid;
double videodatarate,audiodatarate;

inline  static size_t UI24(uint8_t byte[3])
{
	return byte[2]  + byte[1] * 256 + byte[0] * 65536;
}

inline static uint64_t flvtimestamp( uint32_t t )
{
	uint8_t * byte = (uint8_t*) & t;

	return byte[2] + byte[1] * 256 + byte[0] * 65536 + byte[3] * 16777216;
}

static uint32_t toflvtimestamp(uint32_t timestamp)
{
	uint8_t	byte[4];

	byte[3] = (timestamp & 0xFF000000 ) / 16777216;
	byte[2] = timestamp & 0xFF;

	byte[1] = (timestamp  & 0xFF00 ) / 256;
	byte[0] = (timestamp  & 0xFF0000 ) / 65536;

	return  *(uint32_t*)byte;
}


static void update_timestamp(struct flvbodytag * flvbodytag , uint64_t ts)
{

//	ts = flvtimestamp(flvbodytag->timestamp);

	flvbodytag->timestamp = toflvtimestamp(flvtimestamp(flvbodytag->timestamp) + ts/1000);

//	flvbodytag->timestamp
}


static void flv_put_u32(uint32_t c,FILE * flv)
{
	c = htonl(c);
	fwrite(&c,4,1,flv);
}

static void flv_put_string(char * str,FILE * flv)
{
	uint16_t l = strlen(str);
	l = htons(l);

	fwrite(&l,2,1,flv);
	fwrite(str,1,ntohs(l),flv);
}

static void flv_put_double(double d,FILE * flv)
{
	//把 double 的位置按照大端就可以了
	uint64_t * t = (uint64_t*)&d;

	uint64_t n = __bswap_64(*t);

	fwrite(&n,8,1,flv);
}

static void flv_skip_header(FILE * flv)
{
	fseek(flv,9,SEEK_SET);
}

static void flv_write_header()
{
	// header
	fwrite("FLV\001",4,1,out);
	fputc((hasVideo ? 1 : 0) | (hasAudio ? 4 : 0), out);
	fwrite("\000\000\000\011",4,1,out);
}

static void flv_read_metadata(FILE * flv, uint8_t ** metadata, size_t * metadata_len)
{
	struct flvbodytag flvbodytag;

	memset(&flvbodytag,0,sizeof flvbodytag);

	do{
		fseek(flv,UI24(flvbodytag.tag_data_size),SEEK_CUR);
		fread(&flvbodytag,sizeof flvbodytag,1,flv);
	}while(flvbodytag.tag_type != 18);

	*metadata_len = UI24(flvbodytag.tag_data_size);
	*metadata = calloc(*metadata_len,1);

	fread(*metadata,1,*metadata_len,flv);
}

static void flv_write_metadata()
{
	int i,j,offset;

	uint32_t	datasize;
	// data tag
	//	PreviousTagSize
	flv_put_u32(0,out);

	// tag type = 18, script data
	fputc(18,out);

	// 计算datasize
	datasize = 250 + (sizeof(METADATACREATOR) -1)  +  32 + nb_keyframes_all * 9;

	datasize = htonl(datasize);
	fwrite((uint8_t*)(&datasize) + 1,1,3,out);

	//timestamp, always 0
	fwrite("\000\000\000",4,1,out);

	//stream ID, always 0
	fwrite("\000\000\000",3,1,out);

	// SCRIPTDATAOBJECT
	fputc(2,out); // type string
	flv_put_string("onMetaData",out);
	// data
	fputc(8,out);
	flv_put_u32(8,out);

		//metadatacreator
		flv_put_string("metadatacreator",out);
		fputc(2,out);
		flv_put_string(METADATACREATOR,out);

		flv_put_string("hasVideo",out);
		fputc(1,out);
		fputc(hasVideo,out);

		flv_put_string("hasAudio",out);
		fputc(1,out);
		fputc(hasAudio,out);

		flv_put_string("stereo",out);
		fputc(1,out);
		fputc(stereo,out);

		flv_put_string("duration",out);
		fputc(0,out);
		flv_put_double(duration/1000000.0,out);

		flv_put_string("audiosamplerate",out);
		fputc(0,out);
		flv_put_double(audiosamplerate,out);

		flv_put_string("audiocodecid",out);
		fputc(0,out);
		flv_put_double(audiocodecid,out);

		flv_put_string("videocodecid",out);
		fputc(0,out);
		flv_put_double(videocodecid,out);

		flv_put_string("width",out);
		fputc(0,out);
		flv_put_double(width,out);

		flv_put_string("height",out);
		fputc(0,out);
		flv_put_double(height,out);


		flv_put_string("videodatarate",out);
		fputc(0,out);
		flv_put_double(videodatarate,out);

		flv_put_string("audiodatarate",out);
		fputc(0,out);
		flv_put_double(audiodatarate,out);

		// keyframes

		flv_put_string("keyframes",out);
		fputc(3,out);

		flv_put_string("filepositions",out);
		fputc(0xa,out);

		flv_put_u32(nb_keyframes_all,out);

		//开始写入 keyframes_positions
		for( i = 0 , offset = ntohl(datasize) + 9 + sizeof(struct flvbodytag) + 4 ; i < nb_files ; i++)
		{
			for ( j = 0; j < nb_keyframes[i]; j++)
			{
				fputc(0,out);
				flv_put_double(keyframes[i][j].pos + offset,out);
			}
			offset += datasizes[i];
		}


	// SCRIPTDATAOBJECTEND
	fwrite("\000\000\011\000\000\011",1,6,out);

	int ofset =	ftell(out);

	return ;
}

static uint32_t flv_merge_stream(FILE * in, FILE * out,uint64_t timestamp,uint32_t lastsize)
{
	static char buf[65536000];

	struct flvbodytag flvbodytag;

	int pos;

	flv_skip_header(in);//skip the header

	// skip to video content and start reading

	do{
		fread(&flvbodytag,sizeof flvbodytag,1,in);
		fseek(in,UI24(flvbodytag.tag_data_size),SEEK_CUR);
	}while(flvbodytag.tag_type != 18);

	if( lastsize  ){
		fread(&flvbodytag,sizeof flvbodytag,1,in);
		fseek(in,UI24(flvbodytag.tag_data_size),SEEK_CUR);

		fread(&flvbodytag,sizeof flvbodytag,1,in);
		fseek(in,UI24(flvbodytag.tag_data_size),SEEK_CUR);

		pos = ftell(in);
	}

	//开始读咯
	//还要步步更新 timestamp
	if( fread(&flvbodytag,sizeof flvbodytag,1,in) ==1 )
	{
		flvbodytag.prv_tag_size = htonl(lastsize);

		// update timestamp
		update_timestamp(&flvbodytag,timestamp);
		// write tag
		fwrite(&flvbodytag,sizeof flvbodytag,1,out);

		// write tag data
		fread(buf, 1, UI24(flvbodytag.tag_data_size), in);
		fwrite(buf, 1, UI24(flvbodytag.tag_data_size), out);
	}else
		return 0;

	//开始读咯
	//还要步步更新 timestamp
	while( fread(&flvbodytag,sizeof flvbodytag,1,in) ==1 )
	{
		// update timestamp
		update_timestamp(&flvbodytag,timestamp);
		// write tag
		fwrite(&flvbodytag,sizeof flvbodytag,1,out);


		// write tag data
		fread(buf, 1, UI24(flvbodytag.tag_data_size), in);
		fwrite(buf, 1, UI24(flvbodytag.tag_data_size), out);
	}

	return ntohl(flvbodytag.prv_tag_size);
}

int main(int argc, char * argv[])
{


	int i,ret;

	av_register_all();

	nb_files = argc - 1;

	durations = calloc(nb_files,sizeof( uint64_t ));
	datasizes = calloc(nb_files,sizeof( double ));
	nb_keyframes = calloc(nb_files,sizeof( double ));

	keyframes = calloc(nb_files, sizeof(AVIndexEntry*));


	// read metadata of the first package.
	// we only need to write keyframes, duration , videorate , audiorate , width , height and creator:)

	//解析第一个文件

	for (i = 0; i < 1; i++)
	{
		int k;

		AVFormatContext  * flv=NULL;

		char * filename;

		filename = argv[i+1];

		avformat_open_input(&flv,filename,NULL,NULL);

		av_find_stream_info(flv);

		hasAudio = strcmp(av_dict_get(flv->metadata,"hasAudio",NULL,0)->value,"true")==0;
		hasVideo = strcmp(av_dict_get(flv->metadata,"hasVideo",NULL,0)->value,"true")==0;
		stereo = strcmp(av_dict_get(flv->metadata,"stereo",NULL,0)->value,"true")==0;

		durations[i] = flv->duration;

		duration += durations[i];

		width = strtod(av_dict_get(flv->metadata,"width",NULL,0)->value,NULL);
		height = strtod(av_dict_get(flv->metadata,"height",NULL,0)->value,NULL);
		audiosamplerate = strtod(av_dict_get(flv->metadata,"audiosamplerate",NULL,0)->value,NULL);
		audiosamplesize = strtod(av_dict_get(flv->metadata,"audiosamplesize",NULL,0)->value,NULL);

		audiocodecid = strtod(av_dict_get(flv->metadata,"audiocodecid",NULL,0)->value,NULL);
		videocodecid = strtod(av_dict_get(flv->metadata,"videocodecid",NULL,0)->value,NULL);

		videodatarate = strtod(av_dict_get(flv->metadata,"videodatarate",NULL,0)->value,NULL);
		audiodatarate = strtod(av_dict_get(flv->metadata,"audiodatarate",NULL,0)->value,NULL);

		//获得 keyframes position

		nb_keyframes[i] =  flv->streams[0]->nb_index_entries;
		nb_keyframes_all += nb_keyframes[i];

		keyframes[i] = calloc(nb_keyframes[i],sizeof(AVIndexEntry));

		for(k = 0 ; k < nb_keyframes[i]; k++)
		{
			keyframes[i][k] =  flv->streams[0]->index_entries[k];
			keyframes[i][k].pos -=  flv->streams[0]->index_entries[0].pos;
		}

		datasizes[i] = strtod(av_dict_get(flv->metadata,"datasize",NULL,0)->value,NULL);

		avformat_free_context(flv);

	}

	for (i = 1; i < nb_files ; i++)
	{
		int k;

		AVFormatContext  * flv=NULL;

		char * filename;

		filename = argv[i+1];

		avformat_open_input(&flv,filename,NULL,NULL);

		av_find_stream_info(flv);

		durations[i] = flv->duration;

		duration += durations[i];

		//获得 keyframes position

		keyframes[i] = calloc(nb_keyframes[i],sizeof(AVIndexEntry));
		nb_keyframes_all += nb_keyframes[i];

		for(k = 0 ; k < nb_keyframes[i]; k++)
		{
			keyframes[i][k] =  flv->streams[0]->index_entries[k];
			keyframes[i][k].pos -=  flv->streams[0]->index_entries[0].pos;
		}

		datasizes[i] = strtod(av_dict_get(flv->metadata,"datasize",NULL,0)->value,NULL) - flv->streams[0]->index_entries->pos ;

		avformat_free_context(flv);

	}

	//正常的开始写 flv 文件 吧

	out = fopen("out.flv","w");

	flv_write_header();

	flv_write_metadata();

	uint64_t timestamp;
	uint32_t lastsize;

	for (lastsize = 0,i = 0 ,timestamp = 0; i < argc - 1; timestamp += durations[i],i++ )
	{
		char * filename;

		filename = argv[i+1];

		FILE * in = fopen(filename,"r");
		lastsize = flv_merge_stream(in,out,timestamp,lastsize);
	}

	fclose(out);

	return EXIT_SUCCESS;
}

