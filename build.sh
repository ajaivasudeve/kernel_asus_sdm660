#!/bin/bash

# configure git
git config --global user.name "ajaivasudeve"
git config --global user.email "ajaivasudeve@gmail.com"

# setup build environment
apt-get update
apt-get install -y build-essential bc python curl zip gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi libssl-dev wget

# clone clang toolchain
git clone --depth=1 https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86 -b master

# set toolchain paths
clang_path="$CIRCLE_WORKING_DIRECTORY/linux-x86/clang-r370808/bin/clang"
gcc_path="aarch64-linux-gnu-"
gcc_32_path="arm-linux-gnueabi-"

# build begin
args="-j$(nproc --all) O=out \
	ARCH=arm64 \
	SUBARCH=arm64 "

args+="CC=$clang_path \
	CLANG_TRIPLE=aarch64-linux-gnu- \
	CROSS_COMPILE=$gcc_path "

args+="CROSS_COMPILE_ARM32=$gcc_32_path "

clean(){
	make mrproper
	make $args mrproper
}

changelog(){
	git log --pretty=format:"- %h %s" --since="1 week 1 day" > changelog.txt
	if [ ! -s changelog.txt ] 
	then
		echo "- No recent changes!" > changelog.txt
	fi
	wget https://bitbucket.org/$BB_USER/$BB_REPO/raw/HEAD/${1}_changelog.txt
	mv ${1}_changelog.txt changelog_old.txt
	date="$(date +"%d-%m-%Y")"
	echo "[$date]" >> changelog_new.txt
	cat changelog_new.txt changelog.txt >> final_changelog.txt
	echo "" >> final_changelog.txt
	echo "" >> final_changelog.txt
	cat final_changelog.txt changelog_old.txt > ${1}_changelog.txt
	curl -u $BB_AUTH_STRING -X POST https://api.bitbucket.org/2.0/repositories/$BB_USER/$BB_REPO/src -F ${1}_changelog.txt=@${1}_changelog.txt
	rm -rf changelog_new.txt changelog_old.txt final_changelog.txt ${1}_changelog.txt changelog.txt
}

build_z2_plus(){
	export KBUILD_BUILD_USER="ajaivasudeve"
	export KBUILD_BUILD_HOST="slowpoke"
	START=$(date +"%s")
	clean
	make $args z2_plus_defconfig&&make $args
	if [ $? -ne 0 ]; then
    terminate "Error while building for z2_plus!"
    fi
	mkzip "z2_plus"
}

build_z2_row(){
	export KBUILD_BUILD_USER="ajaivasudeve"
	export KBUILD_BUILD_HOST="slowpoke"
	START=$(date +"%s")
	clean
	make $args z2_row_defconfig&&make $args
	if [ $? -ne 0 ]; then
    terminate "Error while building for z2_row!"
    fi
	mkzip "z2_row"
}

mkzip (){
	END=$(date +"%s")
	KDURATION=`expr $END - $START`
	zipname="${1}-${type}-droopy-kernel-$date-$time.zip"
	duration="`expr $KDURATION / 60`m `expr $KDURATION % 60`s"
	cp -f out/arch/arm64/boot/Image.gz-dtb flasher
	cd flasher
	zip -r "$zipname" *
	mv -f "$zipname" $CIRCLE_WORKING_DIRECTORY
	cd $CIRCLE_WORKING_DIRECTORY
	if [ "$weekly" = true ]; then
		changelog "${1}"
		curl -X POST "https://$BB_AUTH_STRING@api.bitbucket.org/2.0/repositories/$BB_USER/$BB_REPO/downloads" --form files=@"$zipname"
		tg_notify "$zipname" "${1}" "$duration"
	else
		tg_upload "$zipname" "${1}" "$duration"	
	fi		
}

tg_upload(){
	curl -F document=@"${1}" "https://api.telegram.org/bot$token/sendDocument" \
        -F chat_id="${chat_id_1}" \
        -F "disable_web_page_preview=true" \
        -F "parse_mode=HTML" \
        -F caption="
<b>Device:</b> 
<code>${2}</code>
<b>Build Type:</b>
<code>$type</code>
<b>Branch:</b> 
<code>$CIRCLE_BRANCH</code>
<b>Build Duration:</b> 
<code>${3}</code>
<b>Last Commit:</b> 
<a href='https://github.com/$CIRCLE_USERNAME/$CIRCLE_PROJECT_REPONAME/commits/$CIRCLE_SHA1'>$(git log --pretty=format:'%h' -1)</a>"	
}

tg_notify(){
    curl -s -X POST "https://api.telegram.org/bot${token}/sendMessage" \
                        -d chat_id="${chat_id}" \
                        -d "disable_web_page_preview=true" \
                        -d "parse_mode=HTML" \
                        -d text="
<b>Device:</b> 
<code>${2}</code>
<b>Build Type:</b>
<code>$type</code>
<b>Branch:</b> 
<code>$CIRCLE_BRANCH</code>
<b>Build Duration:</b> 
<code>${3}</code>
<b>Last Commit:</b> 
<a href='https://github.com/$CIRCLE_USERNAME/$CIRCLE_PROJECT_REPONAME/commits/$CIRCLE_SHA1'>$(git log --pretty=format:'%h' -1)</a>

<a href='$MIRROR/${1}'>Download</a> | <a href='https://bitbucket.org/$BB_USER/$BB_REPO/raw/HEAD/${2}_changelog.txt'>Changelog</a>"
}

terminate(){
  tg_notify "${1}"
  exit 1
}

	date="$(date +"%d-%m-%Y")"
	time="$(date +"%H-%M-%S")"
	type="treble"
	build_z2_plus

	date="$(date +"%d-%m-%Y")"
	time="$(date +"%H-%M-%S")"
	type="treble"
	build_z2_row

# build end	