#!/usr/bin/perl -w

$message='';
while(<>)
{
	if(/------------------------------------------------------------------------/)
	{
		if($message ne "")
		{
			$message=~s/\n+/ /g;
			$message=~s/^[\s\n\r]+//;
			$message=~s/[\s\n\r]+$//;
			$message=~s/\*/{{{*}}}/g;
			$message=~s/_/{{{_}}}/g;
			print "  * *$revision* $author $timestamp\n  _${message}_\n";
			$message="";
		}
	}
	elsif(/^(\S+) \| (\S+) \| ([^\|]+) \|/)
	{
		$revision=$1;
		$author=$2;
		$timestamp=$3;
	}
	else
	{
		$message.=$_;
	}
}
