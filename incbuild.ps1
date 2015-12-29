$lw = [datetime]::minvalue
start powershell -argumentlist "-command",  "&{ gulp incbuild }"
while ($true) {
	$fi = (dir -literalpath "build.log")[0]
	if ($lw -ne $fi.lastwritetime) {
		clear
		write-host "build.log" -background black -foreground red
		$lg = type "build.log"
		$maxh = $host.ui.rawui.windowsize.height - 3
		$maxw = $host.ui.rawui.windowsize.width - 1
		$lg | ? {
			$_ -imatch "error:"	
		} | select -first $maxh | % {
			$_.substring(0, [math]::min($_.length, $maxw))
		}
		write-host $lg.length -background black -foreground red
		$lw = $fi.lastwritetime
	}
	sleep 1
}

