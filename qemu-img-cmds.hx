HXCOMM Use DEFHEADING() to define headings in both help text and texi
HXCOMM Text between STEXI and ETEXI are copied to texi version and
HXCOMM discarded from C version
HXCOMM DEF(command, callback, arg_string) is used to construct
HXCOMM command structures and help message.
HXCOMM HXCOMM can be used for comments, discarded from both texi and C

STEXI
@table @option
ETEXI

DEF("bench", img_bench,
    "bench [-c count] [-d depth] [-f fmt] [--flush-interval=flush_interval] [-n] [--no-drain] [-o offset] [--pattern=pattern] [-q] [-s buffer_size] [-S step_size] [-t cache] [-w] filename")
STEXI
@item bench [-c @var{count}] [-d @var{depth}] [-f @var{fmt}] [--flush-interval=@var{flush_interval}] [-n] [--no-drain] [-o @var{offset}] [--pattern=@var{pattern}] [-q] [-s @var{buffer_size}] [-S @var{step_size}] [-t @var{cache}] [-w] @var{filename}
ETEXI

DEF("check", img_check,
    "check [-q] [-L] [--object objectdef] [--image-opts] [-f fmt] [--output=ofmt] [-r [leaks | all]] [-T src_cache] filename")
STEXI
@item check [--object @var{objectdef}] [--image-opts] [-q] [-L] [-f @var{fmt}] [--output=@var{ofmt}] [-r [leaks | all]] [-T @var{src_cache}] @var{filename}
ETEXI

DEF("create", img_create,
    "create [-q] [-L] [--object objectdef] [--image-opts] [-f fmt] [-o options] filename [size]")
STEXI
@item create [--object @var{objectdef}] [--image-opts] [-q] [-L] [-f @var{fmt}] [-o @var{options}] @var{filename} [@var{size}]
ETEXI

DEF("commit", img_commit,
    "commit [-q] [-L] [--object objectdef] [--image-opts] [-f fmt] [-t cache] [-b base] [-d] [-p] filename")
STEXI
@item commit [--object @var{objectdef}] [--image-opts] [-q] [-L] [-f @var{fmt}] [-t @var{cache}] [-b @var{base}] [-d] [-p] @var{filename}
ETEXI

DEF("compare", img_compare,
    "compare [--object objectdef] [--image-opts] [-f fmt] [-F fmt] [-T src_cache] [-p] [-q] [-L] [-s] filename1 filename2")
STEXI
@item compare [--object @var{objectdef}] [--image-opts] [-f @var{fmt}] [-F @var{fmt}] [-T @var{src_cache}] [-p] [-q] [-L] [-s] @var{filename1} @var{filename2}
ETEXI

DEF("convert", img_convert,
    "convert [--object objectdef] [--image-opts] [-c] [-p] [-q] [-L] [-n] [-f fmt] [-t cache] [-T src_cache] [-O output_fmt] [-o options] [-s snapshot_id_or_name] [-l snapshot_param] [-S sparse_size] filename [filename2 [...]] output_filename")
STEXI
@item convert [--object @var{objectdef}] [--image-opts] [-c] [-p] [-q] [-L] [-n] [-f @var{fmt}] [-t @var{cache}] [-T @var{src_cache}] [-O @var{output_fmt}] [-o @var{options}] [-s @var{snapshot_id_or_name}] [-l @var{snapshot_param}] [-S @var{sparse_size}] @var{filename} [@var{filename2} [...]] @var{output_filename}
ETEXI

DEF("info", img_info,
    "info [--object objectdef] [--image-opts] [-f fmt] [-L] [--output=ofmt] [--backing-chain] filename")
STEXI
@item info [--object @var{objectdef}] [--image-opts] [-f @var{fmt}] [-L] [--output=@var{ofmt}] [--backing-chain] @var{filename}
ETEXI

DEF("map", img_map,
    "map [--object objectdef] [--image-opts] [-f fmt] [-L] [--output=ofmt] filename")
STEXI
@item map [--object @var{objectdef}] [--image-opts] [-f @var{fmt}] [-L] [--output=@var{ofmt}] @var{filename}
ETEXI

DEF("snapshot", img_snapshot,
    "snapshot [--object objectdef] [--image-opts] [-q] [-L] [-l | -a snapshot | -c snapshot | -d snapshot] filename")
STEXI
@item snapshot [--object @var{objectdef}] [--image-opts] [-q] [-L] [-l | -a @var{snapshot} | -c @var{snapshot} | -d @var{snapshot}] @var{filename}
ETEXI

DEF("rebase", img_rebase,
    "rebase [--object objectdef] [--image-opts] [-q] [-L] [-f fmt] [-t cache] [-T src_cache] [-p] [-u] -b backing_file [-F backing_fmt] filename")
STEXI
@item rebase [--object @var{objectdef}] [--image-opts] [-q] [-L] [-f @var{fmt}] [-t @var{cache}] [-T @var{src_cache}] [-p] [-u] -b @var{backing_file} [-F @var{backing_fmt}] @var{filename}
ETEXI

DEF("resize", img_resize,
    "resize [--object objectdef] [--image-opts] [-q] [-L] filename [+ | -]size")
STEXI
@item resize [--object @var{objectdef}] [--image-opts] [-q] [-L] @var{filename} [+ | -]@var{size}
ETEXI

DEF("amend", img_amend,
    "amend [--object objectdef] [--image-opts] [-p] [-q] [-L] [-f fmt] [-t cache] -o options filename")
STEXI
@item amend [--object @var{objectdef}] [--image-opts] [-p] [-q] [-L] [-f @var{fmt}] [-t @var{cache}] -o @var{options} @var{filename}
@end table
ETEXI
