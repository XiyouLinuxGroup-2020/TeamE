/*************************************************************************
有n块石头分别在x轴的0,1,...,n-1位置
一只青蛙在石头0，想跳到石头n-1
如果青蛙在第i块石头上，它最多可以向右跳距离ai
问青蛙能否跳到石头n-1
 ************************************************************************/

#include<stdio.h>
int main()
{
	puts("请输入石头数量：");
	int n;
	scanf("%d", &n);
	int a[n], dp[n] = {0};
	puts("请依次输入ai的值：");
	for (int i = 0; i < n; i++)
		scanf("%d", &a[i]);
	dp[0] = 1;
	for (int i = 1; i < n;i++)
	{
		for (int j = 0; j < i; j++)
		{
			if (dp[j] && a[j]+j >= i)
			{
				dp[j] = 1;
				break;
			}
		}
	}
	if (dp[n-1])
		printf("青蛙能跳到第%d块石头上\n", n);
	else
		printf("青蛙不能跳到第%d块石头上\n", n);
	return 0;
}