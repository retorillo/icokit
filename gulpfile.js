var gulp  = require('gulp');
var watch = require('gulp-watch');
var exec  = require('child_process').exec;

function build() {
	console.log(new Array(61).join("-"));
	exec('build', {
	}, function(err, stdout, stderr){
		console.log(stdout);
		console.log(stderr);
	});
}
gulp.task('build', function(cb) {
	build();
});

gulp.task('incbuild', function(cb) {
	watch('*.cc', { events: [ 'change' ] }, build);
});
