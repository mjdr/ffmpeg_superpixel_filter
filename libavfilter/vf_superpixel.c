#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "avfilter.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "video.h"

typedef struct {
	uint32_t x,y;
	uint32_t r,g,b;
	uint32_t n;
} point_t;

typedef struct {
	const AVClass *class;
	point_t* points;
	uint32_t points_count;
	uint32_t s_x;
	uint32_t s_y;
	
} SuperpixelContext;


static av_cold int init(AVFilterContext* ctx) {
	SuperpixelContext *s = ctx->priv;
	
	
	return 0;
}
static av_cold void uninit(AVFilterContext *ctx) {
	SuperpixelContext *s = ctx->priv;
	
	if(s->points)
		av_free(s->points);
}

static void calc_mins(SuperpixelContext* ctx, uint8_t* in, uint8_t* out, AVFrame *outf) {

	const int k = ctx->points_count*ctx->points_count;
	if(ctx->points == NULL) {
		ctx->points = (point_t*) av_malloc(sizeof(point_t)*k);
		for(int i = 0;i < ctx->points_count;i++)
			for(int j = 0;j < ctx->points_count;j++) {
				point_t p = ctx->points[i*ctx->points_count+j];
				p.x = ((i+1)*outf->width)/(ctx->points_count+2);
				p.y = ((j+1)*outf->height)/(ctx->points_count+2);
				p.r = in[4*(p.y*ctx->points_count+p.x)+0];
				p.g = in[4*(p.y*ctx->points_count+p.x)+1];
				p.b = in[4*(p.y*ctx->points_count+p.x)+2];
			}	
		ctx->s_x = 2*(outf->width/(ctx->points_count+2));
		ctx->s_y = 2*(outf->height/(ctx->points_count+2));
	}

	point_t* current = (point_t*) av_malloc(sizeof(point_t)*k);
	point_t* next = (point_t*) av_malloc(sizeof(point_t)*k);
	
	memcpy(current, ctx->points, sizeof(point_t)*k);
	
	
	uint32_t diff;
	while(1){
		memset(next, 0, sizeof(point_t)*k);
		for(int i = 0;i < outf->width*outf->height;i++) {
			int closest_id = -1;
			uint32_t min_dist = 0xffffffff;
			for(int j = 0;j < k;j++) {
				uint32_t d = 
					(in[4*i+0]-current[j].r) * (in[4*i+0]-current[j].r) +
					(in[4*i+1]-current[j].g) * (in[4*i+1]-current[j].g) +
					(in[4*i+2]-current[j].b) * (in[4*i+2]-current[j].b);
				if(d < min_dist) {
					min_dist = d;
					closest_id = j;
				}
			}
			if(closest_id != -1) {
				next[closest_id].r += in[4*i+0];
				next[closest_id].g += in[4*i+1];
				next[closest_id].b += in[4*i+2];
				next[closest_id].n++;
			}
		}
		diff = 0;
		for(int j = 0;j < k;j++) {
			if(next[j].n == 0) 
				continue;
			diff += abs(current[j].r - next[j].r / next[j].n);
			diff += abs(current[j].g - next[j].g / next[j].n);
			diff += abs(current[j].b - next[j].b / next[j].n);
			
			current[j].r = next[j].r / next[j].n;
			current[j].g = next[j].g / next[j].n;
			current[j].b = next[j].b / next[j].n;
		}
		if(diff == 0) break;
		//printf("Diff: %d\n", diff);
	}
	
	
	for(int i = 0;i < outf->width*outf->height;i++) {
		int closest_id = -1;
		uint32_t min_dist = 0xffffffff;
		for(int j = 0;j < k;j++) {
			uint32_t d = 
				(in[4*i+0]-current[j].r) * (in[4*i+0]-current[j].r) +
				(in[4*i+1]-current[j].g) * (in[4*i+1]-current[j].g) +
				(in[4*i+2]-current[j].b) * (in[4*i+2]-current[j].b);
			if(d < min_dist) {
				min_dist = d;
				closest_id = j;
			}
		}
		if(closest_id != -1) {
			out[4*i+0] = current[closest_id].r;
			out[4*i+1] = current[closest_id].g;
			out[4*i+2] = current[closest_id].b;
		}
		else {
			out[4*i+0] = 255;
			out[4*i+1] = 0;
			out[4*i+2] = 0;
		}
		out[4*i+3] = 255;
	}
	
	memcpy(ctx->points, current, sizeof(point_t)*k);
	av_free(current);
	av_free(next);
}



static int query_formats(AVFilterContext *ctx) {
	static const enum AVPixelFormat pix_fmts[] = {
		AV_PIX_FMT_BGR32,
		AV_PIX_FMT_NONE
	};
	return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{

		SuperpixelContext *ctx = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *out = ff_get_video_buffer(outlink, in->width, in->height);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    
   	calc_mins(ctx, in->data[0], out->data[0], out);
    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

static const AVOption superpixel_src_options[] = {
		{
			"n", 
			"number of superpixels", 
			offsetof(SuperpixelContext, points_count), 
			AV_OPT_TYPE_INT, 
			{.i64 = 5}, 
			1, 
			INT_MAX, 
			AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
		},
    { NULL },
};

AVFILTER_DEFINE_CLASS(superpixel_src);

AVFilter ff_vf_superpixel = {
    .name        	 = "superpixel",
    .priv_class    = &superpixel_src_class,
    .priv_size     = sizeof(SuperpixelContext),
    .init 			 	 = init,
    .uninit 			 = uninit,
    .description 	 = NULL_IF_CONFIG_SMALL("My awesome superpixel filter."),
    .inputs      	 = inputs,
    .outputs     	 = outputs,
    .query_formats = query_formats,
};
