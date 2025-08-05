#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <string.h>
#include <dirent.h>


#define MAX_FILES 1024  // 처리 가능한 최대 파일 수


// 특정 파일에서 target 문자열의 발생 횟수를 세는 함수
void countIncludeOccurrences(const char *filename, const char *target, int *count) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) return;

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    // 파일을 한 줄씩 읽으며 target 문자열 탐색
    while ((read = getline(&line, &len, file)) != -1) {
        char *pos = line;
        while ((pos = strstr(pos, target)) != NULL) {
            (*count)++;
            pos += strlen(target);  // 다음 탐색 시작 위치를 갱신
        }
    }

    fclose(file);
    free(line);  // getline으로 할당된 메모리 해제
}


// 메인 함수
int main(int argc, char *argv[]) {
    
  if (argc != 2) {
        fprintf(stderr, "사용법: %s <디렉토리 경로>\n", argv[0]);
        return 1;
    }

    const char *directory = argv[1];
    const char *target = "#include"; // #include를 target으로 지정

    DIR *dir = opendir(directory);
    if (dir == NULL) {
        perror("디렉토리 열기 실패");
        return 1;
    }

    // 디렉토리 내 .h 파일 목록 수집
    struct dirent *entry;
    char *fileList[MAX_FILES];
    int totalFiles = 0;

    while ((entry = readdir(dir)) != NULL && totalFiles < MAX_FILES) {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".h") != NULL) {
            fileList[totalFiles] = strdup(entry->d_name);  // 파일명 복사
            totalFiles++;
        }
    }

    closedir(dir);

    if (totalFiles == 0) {
        printf("헤더 파일이 없습니다.\n");
        return 0;
    }

    
    // 멀티프로세싱 측정 시작
    
    struct timeval m_start, m_end;
    gettimeofday(&m_start, NULL);

    int filesPerProcess = totalFiles / 4;

    // 총 4개의 자식 프로세스 생성 - fork() 사용
    for (int i = 0; i < 4; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork 실패");
            exit(1);
        }

        if (pid == 0) {
            // 각 프로세스가 담당할 파일 범위 계산
            int startIndex = i * filesPerProcess;
            int endIndex = (i == 3) ? totalFiles : (i + 1) * filesPerProcess;

            int localCount = 0;
            char path[512];

            // 할당된 파일 각각에 대해 #include 개수 세기
            for (int j = startIndex; j < endIndex; ++j) {
                snprintf(path, sizeof(path), "%s/%s", directory, fileList[j]);
                countIncludeOccurrences(path, target, &localCount);
            }

            printf("자식 %d에서 #include 발생 횟수: %d\n", getpid(), localCount);
            exit(localCount);  // 부모에게 count 전달 (단, 255 초과 시 주의)
        }
    }


    // 부모 프로세스: 자식들의 결과 수집
    int totalCount = 0, status;
    for (int i = 0; i < 4; ++i) {
        wait(&status);
        totalCount += WEXITSTATUS(status);  // exit()로 전달된 값 수집
    }


    // 실행 시간 및 자원 사용량 측정 종료
    gettimeofday(&m_end, NULL);
    struct rusage m_usage;
    getrusage(RUSAGE_CHILDREN, &m_usage);

    printf("\n*****************************************************\n");
    printf("#include 총 발생 횟수 : %d\n", totalCount);
    printf("총 수행 시간 : %ld 마이크로초\n",
           (m_end.tv_sec - m_start.tv_sec) * 1000000L + (m_end.tv_usec - m_start.tv_usec));
    printf("총 사용자 모드 시간 : %ld 마이크로초\n", m_usage.ru_utime.tv_usec);
    printf("총 시스템 모드 시간 : %ld 마이크로초\n", m_usage.ru_stime.tv_usec);

    
    // 싱글 프로세싱 추가
    struct timeval s_start, s_end;
    gettimeofday(&s_start, NULL);

    int singleCount = 0;
    char path[512];
    for (int i = 0; i < totalFiles; ++i) {
        snprintf(path, sizeof(path), "%s/%s", directory, fileList[i]);
        countIncludeOccurrences(path, target, &singleCount);
    }

    gettimeofday(&s_end, NULL);
    struct rusage s_usage;
    getrusage(RUSAGE_SELF, &s_usage);

    printf("\n[싱글프로세싱] #include 총 발생 횟수 : %d\n", singleCount);
    printf("[싱글프로세싱] 총 수행 시간 : %ld 마이크로초\n",
           (s_end.tv_sec - s_start.tv_sec) * 1000000L + (s_end.tv_usec - s_start.tv_usec));
    printf("[싱글프로세싱] 사용자 모드 시간 : %ld 마이크로초\n", s_usage.ru_utime.tv_usec);
    printf("[싱글프로세싱] 시스템 모드 시간 : %ld 마이크로초\n", s_usage.ru_stime.tv_usec);


    // 파일명 메모리 해제
    for (int i = 0; i < totalFiles; i++) {
        free(fileList[i]);
    }

    return 0;
}
