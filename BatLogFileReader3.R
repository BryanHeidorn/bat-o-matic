# Need to install the anytime package
#install.packages("anytime", lib="/Library/Frameworks/R.framework/Versions/3.3/Resources/library")
library(anytime)
#install.packages("tcltk2", lib="/Library/Frameworks/R.framework/Versions/3.3/Resources/library")
library(tcltk2)
filename <- tclvalue(tkgetOpenFile(initialdir="/Users/bryanheidorn/Documents/Proposals/BatFeeding/data")) # 
if (!nchar(filename)) {
  tkmessageBox(message = "No file was selected!")
} 
## Line one is a header
## Line 2 is milli, unixtme, date and time but in Universal time not this time zone
## Line 3 is another header (Millis Sensor)
## Line 4 is milli since program start - tab - sensor on or off ([+/-][1-6])
header <- scan(filename, nlines = 1, skip=1, what = character(), sep =",")
## basetime <- anytime(as.numeric(gsub(",","",header[[2]]))+60*60*7) # add 7 hours for time zone
baseunixtime = as.numeric(gsub(",","",header[[2]]))+60*60*7
## calculate tomorrow at 5:30AM
todayDate = trunc(as.POSIXct(anytime(baseunixtime), format="%Y-%m-%dT%H:%M:%OS"), "days") 
tomorrow530AM = trunc(as.POSIXct(anytime(baseunixtime), format="%Y-%m-%dT%H:%M:%OS"), "days") + 30*60*60+30
licks2 <- read.csv(file=filename, skip=2, sep=",")
options(digits.secs=4)
## remove (-) numbers so only count of breaks
licks2 = licks2[licks2[,2] > 0,]
## licks2[,1] + baseunixtime
licks3 <- anytime(licks2[,1]/1000 + baseunixtime) ## Time column
#Need to add column 1 to the header times.
tms <- as.POSIXct(licks3, format="%Y-%m-%dT%H:%M:%OS")
tms = tms[tms < tomorrow530AM] ## remove anything after 5:00Am
##format(tms)
#TotalLicks = length(licks2[,2])
TotalLicks = length(tms)
brks <- trunc(range(tms), "hours")
## TitleStr <- "Bat Licks\nBasetime" + baseunixtime;
png(paste(substr(todayDate,1,10),"Histogram.png"))
hist(tms, main=paste("Bat Licks total =",TotalLicks,"\nStart:", anytime(baseunixtime)), xlab="10 Min Intervals\nFigure 1: Feeding Histogram", freq=TRUE, breaks=seq(brks[1], brks[2]+3600, by="10 min"))
dev.off()
hist(tms, main=paste("Bat Licks total =",TotalLicks,"\nStart:", anytime(baseunixtime)), xlab="10 Min Intervals\nFigure 1: Feeding Histogram", freq=TRUE, breaks=seq(brks[1], brks[2]+3600, by="10 min"))
table1 = table(licks2[,2])
#as.data.frame(table(licks2[,2]))
#licks2[,2] %in% 4
setwd("/Users/bryanheidorn/Documents/Proposals/BatFeeding/data/images")
png(paste(substr(todayDate,1,10),"PortDist.png"))
#Error: Licks 2 needs to be adjusted to truncate time to dawn.
plot(table(licks2[,2]), main=paste("Frequency for feeding ports\n", baseunixtime), xlab="Port Number", ylab="Cummulative Licks")
dev.off()

