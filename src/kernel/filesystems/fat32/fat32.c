#include <ata.h>
#include <fat32.h>
#include <system.h>
#include <util.h>

// Simple alpha FAT32 driver according to the Microsoft specification
// Copyright (C) 2023 Panagiotis

bool isFat32(mbr_partition *mbr) {
  uint8_t *rawArr = (uint8_t *)malloc(SECTOR_SIZE);
  getDiskBytes(rawArr, mbr->lba_first_sector, 1);

  FAT32 *fat = (FAT32 *)rawArr;

  bool ret = fat->sector_count == 0 || fat->reserved_sectors == 0 ||
             fat->sectors_per_track == 0 || fat->volume_id == 0 ||
             (rawArr[66] != FAT_SIGNATURE1 && rawArr[66] != FAT_SIGNATURE2);

  free(rawArr);

  return !ret;
}

bool initiateFat32(MountPoint *mnt) {
  FAT32 *fat = (FAT32 *)malloc(sizeof(FAT32));
  mnt->fsInfo = fat;

  mbr_partition *partPtr = &mnt->mbr;

  debugf("[fat32] Initializing: disk{%d} partition{%d} lba{%d}...\n", mnt->disk,
         mnt->partition, partPtr->lba_first_sector);
  uint8_t *rawArr = (uint8_t *)malloc(SECTOR_SIZE);
  getDiskBytes(rawArr, partPtr->lba_first_sector, 1);

  // this mistake will be classified as the most MalwarePad thing ever
  // fat = (FAT32 *)rawArr;
  memcpy(fat, rawArr, SECTOR_SIZE); // first 512 bytes can be copied exactly

  if (!isFat32(partPtr)) {
    debugf("[fat32] NEVER initialize a drive without checking properly!\n");
    panic();
    return false;
  }

  // at "fat = (FAT32 *)rawArr;" we already overwrote the main ptr
  fat->fat_begin_lba = partPtr->lba_first_sector + fat->reserved_sectors;
  fat->cluster_begin_lba = partPtr->lba_first_sector + fat->reserved_sectors +
                           (fat->number_of_fat * fat->sectors_per_fat);

  fat->works = true;

  debugf("[fat32] Valid FAT32 formatted drive: [%X] %.11s\n", fat->volume_id,
         fat->volume_label);
  debugf("[fat32::info] sector_count{%d} fats{%d} reserved_sectors{%d} "
         "sectors/fat{%d} sectors/track{%d} sectors/cluster{%d}\n",
         fat->sector_count, fat->number_of_fat, fat->reserved_sectors,
         fat->sectors_per_fat, fat->sectors_per_track,
         fat->sectors_per_cluster);

  free(rawArr);

  return true;
}

void finaliseFat32(MountPoint *mnt) { free(mnt->fsInfo); }

bool findFile(FAT32 *fat, pFAT32_Directory fatdir, uint32_t initialCluster,
              char *filename) {
  uint32_t clusterNum = initialCluster;
  uint8_t *rawArr = (uint8_t *)malloc(fat->sectors_per_cluster * SECTOR_SIZE);
  while (1) {
    uint32_t lba =
        fat->cluster_begin_lba + (clusterNum - 2) * fat->sectors_per_cluster;
    getDiskBytes(rawArr, lba, fat->sectors_per_cluster);

    for (uint16_t i = 0; i < ((fat->sectors_per_cluster * SECTOR_SIZE) / 32);
         i++) {
      if (rawArr[32 * i] == FAT_DELETED || rawArr[32 * i + 11] == 0x0F)
        continue;
#if FAT32_DBG_PROMPTS
      debugf("[fat32::findFile] Scanning file: clusterNum{%d} i{%d} fc{%c}\n",
             clusterNum, i, rawArr[32 * i]);
#endif
      if (isLFNentry(fat, rawArr, clusterNum, i)
              ? lfnCmp(fat, clusterNum, i, filename)
              : (memcmp(rawArr + (32 * i), formatToShort8_3Format(filename),
                        11) == 0)) {
        *fatdir = *(pFAT32_Directory)(&rawArr[32 * i]);
        fatdir->lba = lba;
        fatdir->currEntry = i;
#if FAT32_DBG_PROMPTS
        debugf("[fat32::findFile] filename{%s}\n", filename);
        debugf("[fat32::findFile] firstClusterLow{%d} low1{%x} low2{%x}\n",
               (*fatdir).firstClusterLow, rawArr[32 * i + 26],
               rawArr[32 * i + 27]);
        for (int o = 0; o < 32; o++) {
          debugf("%x ", rawArr[32 * i + o]);
        }
        debugf("\n");
#endif
        free(rawArr);
        return true;
      }
    }

    if (rawArr[fat->sectors_per_cluster * SECTOR_SIZE - 32] != 0) {
      uint32_t nextCluster = getFatEntry(fat, clusterNum);
      if (nextCluster == 0) {
        memset(fatdir, 0, sizeof(FAT32_Directory));
        free(rawArr);
        return false;
      }
      clusterNum = nextCluster;
    } else {
      memset(fatdir, 0, sizeof(FAT32_Directory));
      free(rawArr);
      return false;
    }
  }
}

void readFileContents(FAT32 *fat, char **rawOut, pFAT32_Directory dir) {
#if FAT32_DBG_PROMPTS
  debugf("[fat32::read] Reading file contents: filesize{%d} cluster{%d}\n",
         dir->filesize, dir->firstClusterLow);
#endif
  if (dir->attributes != 0x20) {
    debugf(
        "[fat32::read] Seriously tried to read non-file entry (0x%02X attr)\n",
        dir->attributes);
    return;
  }

  char    *out = *rawOut;
  uint32_t curr = 0;
  uint32_t currCluster = dir->firstClusterLow;
  uint8_t *rawArr = (uint8_t *)malloc(fat->sectors_per_cluster * SECTOR_SIZE);
  while (1) { // DIV_ROUND_CLOSEST(dir->filesize, SECTOR_SIZE)
    uint32_t lba =
        fat->cluster_begin_lba + (currCluster - 2) * fat->sectors_per_cluster;
    getDiskBytes(rawArr, lba, fat->sectors_per_cluster);
#if FAT32_DBG_PROMPTS
    debugf("[fat32::read] Scanning file: hex{0x%x} dec{%d}\n", currCluster,
           currCluster);
#endif
    for (uint16_t j = 0; j < fat->sectors_per_cluster * SECTOR_SIZE; j++) {
      if (curr > dir->filesize) {
#if FAT32_DBG_PROMPTS
        debugf("[fat32::read] Reached filesize end (limit) at: hex{0x%x} "
               "dec{%d} (in bytes)\n",
               currCluster, currCluster);
#endif
        free(rawArr);
        return;
      }
      out[curr] = rawArr[j];
      curr++;
    }
    uint32_t old = currCluster;
    currCluster = getFatEntry(fat, old);
    if (currCluster == 0) {
#if FAT32_DBG_PROMPTS
      debugf("[fat32::read] Reached hard end (0x0), showing last cluster: "
             "hex{0x%x} dec{%d}\n",
             old, old);
#endif
      break;
    }
  }
  free(rawArr);
}

bool fatOpenFile(FAT32 *fat, pFAT32_Directory dir, char *filename) {
  if (filename[0] != '/')
    return 0;

  uint32_t index = 0;
  uint32_t len = strlength(filename);
  char    *tmpBuff = (char *)malloc(len + 1);
  dir->firstClusterLow = 2;
  memset(tmpBuff, '\0', len);

  for (uint32_t i = 1; i < len; i++) { // skip index 0
    if (filename[i] == '/' || (i + 1) == len) {
      if ((i + 1) == len)
        tmpBuff[index++] = filename[i];
      if (!findFile(fat, dir, dir->firstClusterLow, tmpBuff)) {
        debugf("[fat32::open] Could not open file %s!\n", filename);
        free(tmpBuff);
        return false;
      }

      // cleanup
      memset(tmpBuff, '\0', len);
      index = 0;
    } else
      tmpBuff[index++] = filename[i];
  }

  free(tmpBuff);
  return true;
}

bool deleteFile(FAT32 *fat, char *filename) {
  FAT32_Directory fatdir;
  if (!fatOpenFile(fat, &fatdir, filename))
    return false;
  uint8_t *rawArr = (uint8_t *)malloc(fat->sectors_per_cluster * SECTOR_SIZE);
  getDiskBytes(rawArr, fatdir.lba, fat->sectors_per_cluster);
  rawArr[32 * fatdir.currEntry] = FAT_DELETED;
  write_sectors_ATA_PIO(fatdir.lba, fat->sectors_per_cluster, rawArr);

  // invalidate
  uint32_t currCluster = fatdir.firstClusterLow;
  while (1) {
    setFatEntry(fat, currCluster, 0);
    uint32_t old = currCluster;
    currCluster = getFatEntry(fat, old);
    if (currCluster == 0)
      break;
  }

  return true;
}
